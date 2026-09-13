-- Standard Lua; the Aue boundary layer is expected to be transparent.
-- The same script is executed under L0 (direct binding) and L1 (contract
-- dispatch through __index) and the outputs must match byte-for-byte.

local c = counter.new(10)
counter.add(c, 5)
counter.add(c, -2)
print("value", counter.get(c))
print("module", counter._module)
print("hash", counter._abi_hash)

local total = 0
for i = 1, 100 do
    total = total + i
end
print("sum", total)
