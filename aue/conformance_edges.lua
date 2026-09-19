-- Edge cases for the Aue boundary layer: multiple userdata values, negative
-- deltas, repeated reads and a guarded error path. L0 and L1 must agree.
local a = counter.new(0)
local b = counter.new(100)
counter.add(a, 7)
counter.add(b, -50)
print("a", counter.get(a))
print("b", counter.get(b))
print("a_again", counter.get(a))

local ok = pcall(function()
    return counter.get(nil)
end)
print("nil_guard", ok)
