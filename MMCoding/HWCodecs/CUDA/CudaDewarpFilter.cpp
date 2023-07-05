
#include "Transforms.h"
#include "CoordinateTransform.h"

#ifdef _WIN32

#include "../FilterImpl.h"
#include "CudaDevice.h"
#include "CudaProcessor.h"
#include "CudaSampleHolder.h"

#include "HWAccelerated.h"

#include <mutex>

const double PI = std::atan(1) * 4;

class Point2d
{
public:
    double Length2() const
    {
        return X * X + Y * Y;
    }
    double Length() const
    {
        return std::sqrt(Length2());
    }
    Point2d Normalized() const
    {
        double length = Length();
        return (length == 0) ? Point2d{ 0, 0 } : (*this / length);
    }
    Point2d operator *(double v) const
    {
        return Point2d{ X * v, Y * v };
    }
    Point2d operator /(double v) const
    {
        return Point2d{ X / v, Y / v };
    }
    Point2d operator +(const Point2d& p) const
    {
        return { X + p.X, Y + p.Y };
    }
    Point2d operator -(const Point2d& p) const
    {
        return { X - p.X, Y - p.Y };
    }

    operator std::pair<double, double>() const
    {
        return { X, Y };
    }

public:
    double X;
    double Y;
};

Point2d ToPoint(const std::pair<double, double> p)
{
    return { p.first, p.second };
}

double clamp(double value, double min, double max)
{
    return value < min ? min :
        max < value ? max :
        value;
}

const double PERIMETER_TILT_LIMIT = 0.75;

class Dewarp : public NCorbaHelpers::CRefcountedImpl, public NMMSS::ICoordinateTransform
{
public:
    Dewarp(NMMSS::DewarpCircle circles[2])
    {
        for (int i = 0; i < 2; ++i)
        {
            m_circles[i] = circles[i];
        }
        m_circlesCount = 1 + (m_circles[1].Radius > 0);
    }

public:
    Point Transform(const Point& xy) const override
    {
        Point2d panTilt = {xy.first * PI * HemisphereCount(), (xy.second - 0.5) * PI * PERIMETER_TILT_LIMIT};
        int hemisphereIndex = panTilt.X > PI ? 1 : 0;
        return PanTiltToInputVideoPoint(panTilt, hemisphereIndex);
    }
    Rect Transform(const Rect& rect) const override
    {
        Point2d p1 = ToPoint(rect.first);
        Point2d p2 = ToPoint(rect.second);
        Point2d center = (p1 + p2) / 2;
        Point2d halfSize = (p2 - p1) / 2;
        Point2d s2 = halfSize * (sqrt(2.0) / 2);

        const int POINT_COUNT = 8;
        Point2d pts[POINT_COUNT] =
        {
            center + Point2d{halfSize.X, 0},
            center + s2,
            center + Point2d{0, halfSize.Y},
            center + Point2d{-s2.X, s2.Y},
            center + Point2d{-halfSize.X, 0},
            center - s2,
            center + Point2d{0, -halfSize.Y},
            center + Point2d{s2.X, -s2.Y}
        };
        for (int i = 0; i < POINT_COUNT; ++i)
        {
            pts[i] = ToPoint(Transform(pts[i]));
        }
        auto minmaxX = std::minmax_element(pts, pts + POINT_COUNT, [](const Point2d& p1, const Point2d& p2) { return p1.X < p2.X; });
        auto minmaxY = std::minmax_element(pts, pts + POINT_COUNT, [](const Point2d& p1, const Point2d& p2) { return p1.Y < p2.Y; });


        return { { minmaxX.first->X, minmaxY.first->Y }, {minmaxX.second->X, minmaxY.second->Y} };
    }

public:
    Point2d PanTiltToInputVideoPoint(Point2d panTilt, int hemisphereIndex) const
    {
        double x = -std::cos(panTilt.X) * std::cos(panTilt.Y);
        double y = std::sin(panTilt.Y);
        Point2d xy{ x * sphereIndexDirection(hemisphereIndex), y };

        Point2d circleXY = xy.Normalized() * (std::asin(clamp(xy.Length(), 0, 1)) / (PI / 2));
        return {m_circles[hemisphereIndex].X + circleXY.X * m_circles[hemisphereIndex].Radius / HemisphereCount(),
            m_circles[hemisphereIndex].Y + circleXY.Y * m_circles[hemisphereIndex].Radius};
    }
    int HemisphereCount() const
    {
        return m_circlesCount;
    }

private:
    static int sphereIndexDirection(int sphereIndex)
    {
        return sphereIndex > 0 ? -1 : 1;
    }

private:
    NMMSS::DewarpCircle m_circles[2] = {};
    int m_circlesCount = 1;
};

class CudaDewarpTransform : public NLogging::WithLogger
{
public:
    CudaDewarpTransform(DECLARE_LOGGER_ARG, NCorbaHelpers::CAutoPtr<Dewarp> dewarp) :
        NLogging::WithLogger(GET_LOGGER_PTR),
        m_dewarp(dewarp)
    {
    }

    ~CudaDewarpTransform()
    {
        if (!m_map.empty())
        {
            auto context = m_device->SetContext();
            m_map.clear();
            m_memory.clear();
        }
    }

    NMMSS::ETransformResult operator()(NMMSS::ISample* in, NMMSS::CDeferredAllocSampleHolder& holder)
    {
        using THeader = NMMSS::NMediaType::Video::VideoMemory;
        if (in->Header().nMajor == NMMSS::NMediaType::Video::ID && in->Header().nSubtype == THeader::ID &&
            in->SubHeader<THeader>().Type == THeader::EVideoMemoryType::CUDA)
        {
            if(auto device = CudaSample::GetDevice(*in))
            {
                auto context = device->SetContext();
                if (auto processor = device->Processor())
                {
                    if (checkMap(device))
                    {
                        const auto srcLumaSurface = CudaSample::GetSurface(*in, false);
                        int srcWidth = srcLumaSurface.Size.Width;
                        int dstWidth = srcWidth;
                        int dstHeight = (int)(PERIMETER_TILT_LIMIT * srcWidth / m_dewarp->HemisphereCount());

                        auto dstSample = m_sampleHolder->GetFreeSample();
                        dstSample->Setup(dstWidth, dstHeight, in->Header().dtTimeBegin, holder);
                        if (dstSample->IsValid())
                        {
                            CudaTexture srcLuma(*m_device, srcLumaSurface, CUarray_format::CU_AD_FORMAT_UNSIGNED_INT8, 1);
                            CudaTexture srcColor(*m_device, CudaSample::GetSurface(*in, true), CUarray_format::CU_AD_FORMAT_UNSIGNED_INT8, 2);
                            for (int hemisphere = 0; hemisphere < m_dewarp->HemisphereCount(); ++hemisphere)
                            {
                                CudaSurfaceRegion dstLuma = CudaSample::GetSurface(*dstSample->Sample(), false);
                                CudaSurfaceRegion dstColor = CudaSample::GetSurface(*dstSample->Sample(), true);

                                dstLuma.Size.Width /= m_dewarp->HemisphereCount();
                                dstLuma.Ptr += hemisphere * dstLuma.Size.Width;

                                dstColor.Size.Width /= m_dewarp->HemisphereCount();
                                dstColor.Ptr += hemisphere * dstLuma.Size.Width;

                                processor->Map(srcLuma, dstLuma, *m_map[hemisphere], false, dstSample->Stream());
                                processor->Map(srcColor, dstColor, *m_map[hemisphere], true, dstSample->Stream());
                            }
                            dstSample->Stream()->Synchronize();
                            holder.AddSample(dstSample->Sample());
                            return NMMSS::ETRANSFORMED;
                        }
                    }
                }
            }
            return NMMSS::EIGNORED;
        }
        return NMMSS::ETHROUGH;
    }

private:
    bool checkMap(CudaDeviceSP device)
    {
        if (m_map.empty())
        {
            m_device = device;
            m_sampleHolder = m_device->CreateSampleHolder(false);

            const int W = 128;
            const int H = 128;
            SurfaceSize mapSize = { W, H, W * 2 * sizeof(float) };
            for (int hemisphereIndex = 0; hemisphereIndex < m_dewarp->HemisphereCount(); ++hemisphereIndex)
            {
                std::vector<float> mapData(W * H * 2);
                for (int x = 0; x < W; ++x)
                {
                    double pan = PI * x / (W - 1) + PI * hemisphereIndex;
                    for (int y = 0; y < H; ++y)
                    {
                        double tilt = (-PI / 2 + (PI * y / (H - 1))) * PERIMETER_TILT_LIMIT;
                        Point2d texCoord = m_dewarp->PanTiltToInputVideoPoint({ pan, tilt }, hemisphereIndex);
                        mapData[y * W * 2 + x * 2] = (float)texCoord.X;
                        mapData[y * W * 2 + x * 2 + 1] = (float)texCoord.Y;
                    }
                }

                if (CudaMemorySP memory = m_device->AllocateMemory(mapSize.Height * mapSize.Pitch, false))
                {
                    m_device->CheckStatus(cuMemcpyHtoD(memory->DevicePtr(), mapData.data(), mapSize.Height * mapSize.Pitch), "cuMemcpyHtoD");
                    m_memory.push_back(memory);
                    m_map.push_back(std::make_unique<CudaTexture>(*m_device, memory->DevicePtr(), mapSize, CUarray_format::CU_AD_FORMAT_FLOAT, 2));
                }
            }
        }
        return m_map.size() == m_dewarp->HemisphereCount();
    }

private:
    CudaSampleHolderSP m_sampleHolder;
    NCorbaHelpers::CAutoPtr<Dewarp> m_dewarp;
    CudaDeviceSP m_device;
    std::vector<std::unique_ptr<CudaTexture>> m_map;
    std::vector<CudaMemorySP> m_memory;
};

namespace NMMSS
{
    IFilter* CreateCudaDewarpFilter(DECLARE_LOGGER_ARG, DewarpCircle circles[2], ICoordinateTransform** transform)
    {
        NCorbaHelpers::CAutoPtr<Dewarp> dewarp(new Dewarp(circles));
        if (transform)
        {
            *transform = dewarp.Dup();
        }
        return new CPullFilterImpl<CudaDewarpTransform, true>(GET_LOGGER_PTR, SAllocatorRequirements(), SAllocatorRequirements(), 
            new CudaDewarpTransform(GET_LOGGER_PTR, dewarp));
    }
}

#endif
