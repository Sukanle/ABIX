#pragma once

// A provider whose methods are exposed as C-ABI thunks, then bound to a named
// interface. The interface is a named contract (`[[interface]]`); each concrete
// type binds its generated method thunk to the contract's slot (`[[implements]]`).
// No type inherits anything and no private vtable is exposed.
class Square {
public:
    explicit Square(int side)
        : side_(side) {}
    int area() const { return side_ * side_; }

private:
    int side_;
};

class Circle {
public:
    explicit Circle(int radius)
        : radius_(radius) {}
    int area() const { return 3 * radius_ * radius_; }

private:
    int radius_;
};

// The interface method's contract signature: the object handle comes first.
int shape_area(const void *self);
