#ifndef COORDINATE_TRANSFORM_H
#define COORDINATE_TRANSFORM_H

#include "MMCodingExports.h"
#include <CorbaHelpers/Refcounted.h>

namespace NLogging
{
    class ILogger;
};

namespace NMMSS
{

class ICoordinateTransform : public virtual NCorbaHelpers::IRefcounted
{
public:
    using Point = std::pair<double, double>;
    using Rect = std::pair<Point, Point>;

    virtual Point Transform(const Point& xy) const = 0;
    virtual Rect Transform(const Rect& rect) const = 0;
};

using PCoordinateTransform = NCorbaHelpers::CAutoPtr<ICoordinateTransform>;

class DewarpCircle
{
public:
    double X;
    double Y;
    double Radius;
};

class IFilter;

MMCODING_DECLSPEC IFilter* CreateCudaDewarpFilter(NLogging::ILogger* ngp_Logger_Ptr_, DewarpCircle circles[2], ICoordinateTransform** transform = nullptr);

}

#endif
