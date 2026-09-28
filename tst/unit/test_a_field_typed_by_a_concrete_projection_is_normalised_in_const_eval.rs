// bitflags 2 declares `struct WasmFeatures(<WasmFeatures as PublicFlags>::Internal)`,
// and wasmparser's consts call methods reading that field. The type of a field
// is normalised where it is used, as rustc's layout does; ours was skipped
// whenever the field type named no generic parameter.
trait Flags {
    type Internal;
}

#[derive(Clone, Copy)]
struct Bits(u32);

impl Bits {
    const fn get(self) -> u32 {
        self.0
    }
}

struct Features(<Features as Flags>::Internal);

impl Flags for Features {
    type Internal = Bits;
}

impl Features {
    const fn bits(&self) -> u32 {
        self.0.get()
    }
}

const ALL: Features = Features(Bits(7));
const ALL_BITS: u32 = ALL.bits();

fn main() {
    assert_eq!(ALL_BITS, 7);
    assert_eq!(ALL.bits(), 7);
}
