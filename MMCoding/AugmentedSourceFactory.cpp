#include "AugmentedSourceFactory.h"
#include "../Distributor.h"
#include "../ConnectionResource.h"
#include "../ThickProxyPinImpl.h"
#include <CorbaHelpers/RefcountedImpl.h>
#include <boost/thread/lock_guard.hpp>

namespace {

    class CSplitter;
    using PSplitter = NCorbaHelpers::CAutoPtr<CSplitter>;
    using WPSplitter = NCorbaHelpers::CWeakPtr<CSplitter>;

    class CSplitter
        : public NCorbaHelpers::CWeakReferableImpl
        , public NLogging::WithLogger
    {
        void init(NMMSS::IPullStyleSource* source, NMMSS::CAugmentsRange const& augs)
        {
            assert(source != nullptr);
            assert(!augs.empty());
            assert(NMMSS::IsDistributor(augs.back()));

            m_distributor = NMMSS::CreateDistributor(GET_LOGGER_PTR, augs.back());
            if (!m_distributor)
                throw std::runtime_error("CSplitter: Cannot create distributor");

            m_source = NMMSS::CreateAugmentedSource(GET_LOGGER_PTR, source, NMMSS::CAugmentsRange(augs.begin(), augs.end()-1));
            if (!m_source)
                throw std::runtime_error("CSplitter: Cannot augment source");

            m_connection = NMMSS::CConnectionResource(m_source.Get(), m_distributor->GetSink(), GET_LOGGER_PTR);
            if (!m_connection)
                throw std::runtime_error("CSplitter: Cannot connection distributor with source");

            if (m_parent != nullptr)
            {
                boost::lock_guard<boost::mutex> parentGuard(m_parent->m_mutex);
                m_parent->m_children.push_back(WPSplitter(this));
            }
        }
    public:
        CSplitter(DECLARE_LOGGER_ARG, NMMSS::IPullStyleSource* source, NMMSS::CAugmentsRange const& augs)
            : NLogging::WithLogger(GET_LOGGER_PTR)
        {
            init(source, augs);
        }
        CSplitter(DECLARE_LOGGER_ARG, CSplitter* parent, NMMSS::EStartFrom startFrom, NMMSS::CAugmentsRange const& augs)
            : NLogging::WithLogger(GET_LOGGER_PTR)
            , m_parent(parent, NCorbaHelpers::ShareOwnership())
        {
            auto source = NMMSS::PPullStyleSource(parent->CreateSource(startFrom));
            if (!source)
                throw std::runtime_error("CSplitter: Parent has been exhausted");
            init(source.Get(), augs);
        }
        ~CSplitter()
        {
            if (m_parent)
            {
                boost::lock_guard<boost::mutex> parentGuard(m_parent->m_mutex);
                auto& children = m_parent->m_children;
                auto it = std::find(children.begin(), children.end(), WPSplitter(this));
                if (it != children.end())
                    children.erase(it);
            }
        }
        NMMSS::IPullStyleSource* CreateSource(NMMSS::EStartFrom startFrom)
        {
            return m_distributor->CreateSource(startFrom);
        }
        CSplitter* Root()
        {
            return m_parent ? m_parent->Root() : this;
        }
        std::vector<WPSplitter> Children() const
        {
            boost::lock_guard<boost::mutex> guard(m_mutex);
            return m_children;
        }
        NMMSS::CAugments GetAugments() const
        {
            NMMSS::CAugments augs = m_source->GetAugments();
            augs.push_back(m_distributor->GetTweak());
            return augs;
        }
    private:
        boost::mutex mutable m_mutex;
        PSplitter m_parent;
        std::vector<WPSplitter> m_children;
        NMMSS::PAugmentedSource m_source;
        NMMSS::PDistributor m_distributor;
        NMMSS::CConnectionResource m_connection;
    };

    class CAugmentedSourceFactory
        : public virtual NMMSS::IAugmentedSourceFactory
        , public NCorbaHelpers::CRefcountedImpl
        , public NLogging::WithLogger
    {
        using AugmentsIt = NMMSS::CAugmentsRange::const_iterator;

        ////////////////////////////////////////////////////////////////////////

        class CAugmentedSource
            : public NMMSS::CThickProxySourceImpl<NMMSS::IAugmentedSource>
            , public NCorbaHelpers::CRefcountedImpl
        {
            using Base = NMMSS::CThickProxySourceImpl<NMMSS::IAugmentedSource>;
        public:
            CAugmentedSource(DECLARE_LOGGER_ARG, CSplitter* root, NMMSS::EStartFrom startFrom, NMMSS::CAugmentsRange const& augs)
                : Base(nullptr, GET_LOGGER_PTR)
                , m_splitter(root, NCorbaHelpers::ShareOwnership())
            {
                buildProcessingLine(startFrom, augs);
            }
            void Modify(NMMSS::EStartFrom startFrom, NMMSS::CAugmentsRange const& augs) override
            {
                boost::lock_guard<boost::mutex> guard(m_mutex);
                buildProcessingLine(startFrom, augs);
            }
            NMMSS::CAugments GetAugments() const override
            {
                assert(false && "CAugmentedSource::GetAugments() isn't implemented");
                throw std::logic_error("CAugmentedSource::GetAugments() isn't implemented");
            }
        private:
            void buildProcessingLine(NMMSS::EStartFrom startFrom, NMMSS::CAugmentsRange const& augs)
            {
                auto root = NCorbaHelpers::ShareRefcounted(m_splitter->Root());
                auto first = augs.begin();
                auto last = augs.end();

                auto splitter = findCommonSplitter(root, &first, last);
                splitter = buildSplitters(GET_LOGGER_PTR, splitter, startFrom, &first, last);
                
                if (Base::GetWrappedSource() && (m_splitter == splitter))
                {
                    Base::GetWrappedSource()->Modify(startFrom, NMMSS::CAugmentsRange(first, last));
                    return;
                }

                auto distSource = NMMSS::PPullStyleSource(splitter->CreateSource(startFrom));
                if (!distSource)
                    throw std::runtime_error("CAugmentedSource: Parent has been exhausted");

                auto augSource = NMMSS::PAugmentedSource(NMMSS::CreateAugmentedSource(
                    GET_LOGGER_PTR, distSource.Get(), NMMSS::CAugmentsRange(first, last)));
                if (!augSource)
                    throw std::runtime_error("CAugmentedSource: Cannot augment source");

                Base::SetWrappedSource(augSource.Get(), NCorbaHelpers::ShareOwnership());
                m_splitter = splitter;
            }
            static PSplitter findCommonSplitter(PSplitter splitter, AugmentsIt* first, AugmentsIt last)
            {
                for (auto wpChild : splitter->Children())
                {
                    PSplitter child = wpChild;
                    if (matchNode(child, first, last))
                        return findCommonSplitter(child, first, last);
                }
                return splitter;
            }
            static bool matchNode(PSplitter const& splitter, AugmentsIt *first, AugmentsIt last)
            {
                auto it = *first;
                for (auto const& aug : splitter->GetAugments())
                {
                    if (it == last || !(*(it++) == aug))
                        return false;
                }
                *first = it;
                return true;
            }
            static PSplitter buildSplitters(DECLARE_LOGGER_ARG, PSplitter splitter, NMMSS::EStartFrom startFrom, AugmentsIt* first, AugmentsIt last)
            {
                for (auto it = *first; it != last; ++it)
                {
                    if (NMMSS::IsDistributor(*it))
                    {
                        splitter = new CSplitter(GET_LOGGER_PTR, splitter.Get(), startFrom, NMMSS::CAugmentsRange(*first, it + 1));
                        *first = it + 1;
                    }
                }
                return splitter;
            }
        private:
            boost::mutex mutable m_mutex;
            PSplitter m_splitter;
        };

        ////////////////////////////////////////////////////////////////////////

    public:
        CAugmentedSourceFactory(DECLARE_LOGGER_ARG, NMMSS::IPullStyleSource* source, NMMSS::CAugmentsRange const& augs)
            : NLogging::WithLogger(GET_LOGGER_PTR)
            , m_root(new CSplitter(GET_LOGGER_PTR, source, augs))
        {}
        NMMSS::IAugmentedSource* CreateSource(NMMSS::EStartFrom startFrom, NMMSS::CAugmentsRange const& augs) override
        {
            return new CAugmentedSource(GET_LOGGER_PTR, m_root.Get(), startFrom, augs);
        }
    private:
        PSplitter m_root;
    };

} // anonymous namespace

namespace NMMSS {

    IAugmentedSourceFactory* CreateAugmentedSourceFactory(DECLARE_LOGGER_ARG,
        IPullStyleSource* source, CAugmentsRange const& augs)
    {
        return new CAugmentedSourceFactory(GET_LOGGER_PTR, source, augs);
    }

} // namespace NMMSS
