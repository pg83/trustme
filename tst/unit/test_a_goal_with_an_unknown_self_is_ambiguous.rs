// sha2's `buffer_fixed!` deserializes its core with `SerializableState::deserialize`,
// whose argument's size `CtOutWrapper<Sha512VarCore, U48>` shares with the core it
// wraps. `<?S as State>::Size == u8` has a variable self type: rustc's selection
// leaves such a goal ambiguous (`assemble_candidates`) until `Hasher { core }`
// names the type, instead of taking the one impl whose size is `u8` outright.
use std::marker::PhantomData;

struct Arr<N>(PhantomData<N>);

trait State: Sized {
    type Size;
    fn deserialize(state: &Arr<Self::Size>) -> Result<Self, ()>;
}

struct Core;
struct Wrapper<T>(T);

impl State for Core {
    type Size = u8;
    fn deserialize(_: &Arr<u8>) -> Result<Self, ()> {
        Ok(Core)
    }
}

impl<T: State> State for Wrapper<T> {
    type Size = T::Size;
    fn deserialize(state: &Arr<T::Size>) -> Result<Self, ()> {
        T::deserialize(state).map(Wrapper)
    }
}

struct Hasher {
    core: Wrapper<Core>,
}

fn split<N>() -> Arr<N> {
    Arr(PhantomData)
}

fn restore() -> Result<Hasher, ()> {
    let serialized_core = split::<<Wrapper<Core> as State>::Size>();
    let core = State::deserialize(&serialized_core)?;
    Ok(Hasher { core })
}

fn main() {
    assert!(restore().is_ok());
}
