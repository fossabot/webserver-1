#ifndef MMCODING_AUGMENTEDSOURCEFACTORY_H_
#define MMCODING_AUGMENTEDSOURCEFACTORY_H_

#include "MMCodingExports.h"
#include "../Augments.h"
#include "../MMSS.h"

namespace NMMSS {

    class IAugmentedSource : public virtual IPullStyleSource
    {
    public:
        virtual void Modify(EStartFrom, CAugmentsRange const&) = 0;
        virtual CAugments GetAugments() const = 0;
    };
    using PAugmentedSource = NCorbaHelpers::CAutoPtr<IAugmentedSource>;

    class IAugmentedSourceFactory : public virtual NCorbaHelpers::IRefcounted
    {
    public:
        virtual IAugmentedSource* CreateSource(EStartFrom, CAugmentsRange const&) = 0;
    };
    using PAugmentedSourceFactory = NCorbaHelpers::CAutoPtr<IAugmentedSourceFactory>;

    MMCODING_DECLSPEC IAugmentedSource*
    CreateAugmentedSource(
        DECLARE_LOGGER_ARG,
        IPullStyleSource* source,
        CAugmentsRange const& augs);

    MMCODING_DECLSPEC IAugmentedSourceFactory*
    CreateAugmentedSourceFactory(
        DECLARE_LOGGER_ARG,
        IPullStyleSource* source,
        CAugmentsRange const& augs);

} // namespace NMMSS

#endif // MMCODING_AUGMENTEDSOURCEFACTORY_H_
