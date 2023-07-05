#pragma once

struct Point
{
    inline Point(int x = 0, int y = 0) :
        X(x),
        Y(y)
    {
    }

    inline Point operator +(const Point& p) const
    {
        return { X + p.X, Y + p.Y };
    }

    inline Point operator -(const Point& p) const
    {
        return { X - p.X, Y - p.Y };
    }

    inline Point operator /(int n) const
    {
        return { X / n, Y / n };
    }

    inline Point operator *(int n) const
    {
        return { X * n, Y * n };
    }

    inline Point operator *(const Point& p) const
    {
        return { X * p.X, Y * p.Y };
    }

    inline bool IsEmpty() const
    {
        return !X && !Y;
    }

    int X;
    int Y;
};

inline bool operator ==(const Point& p1, const Point& p2)
{
    return p1.X == p2.X && p1.Y == p2.Y;
}

inline bool operator !=(const Point& p1, const Point& p2)
{
    return !operator==(p1, p2);
}

struct Box
{
    inline Box(const Point& position = {}, const Point& size = {}) :
        Position(position),
        Size(size)
    {
    }

    inline Point SecondPoint() const
    {
        return Position + Size;
    }

    Point Position;
    Point Size;
};

class SurfaceSize
{
public:
    operator Point() const
    {
        return { Width, Height };
    }

    int MemorySize() const
    {
        return Pitch * Height;
    }

public:
    int Width{};
    int Height{};
    int Pitch{};
};
