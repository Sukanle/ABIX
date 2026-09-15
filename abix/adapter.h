#ifndef SKL_ABIX_ADAPTER_H
#define SKL_ABIX_ADAPTER_H

// Typed ABI adapter dispatch.
//
// `amc adapter --typed` generates a specialization of `abix::adapter<Source,
// Target>` for each compatible type pair; the specialization applies the ABIX
// field mapping from raw source memory to raw target memory. The primary
// template reports "no adapter" so callers can fall back gracefully.
namespace abix {

template<typename Source, typename Target>
struct adapter {
    static bool apply(void *target, const void *source) noexcept {
        (void)target;
        (void)source;
        return false;
    }
};

}   // namespace abix

#endif   // SKL_ABIX_ADAPTER_H
