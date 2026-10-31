//@ proc-macro-aux-build: numeric_token_bridge.rs
use numeric_token_bridge::echo;
fn main() {
    let answer = echo!({
        use std::mem::size_of;
        #[derive(Clone, Copy)]
        struct Number(u32);
        trait Measure { type Output; fn get(&self) -> Self::Output; }
        impl Measure for Number where Self: Sized {
            type Output = u32;
            fn get(&self) -> u32 { self.0 }
        }
        enum State { Present(u32), Empty }
        union Bits { value: u32, bytes: [u8; 4] }
        pub(crate) mod nested {
            pub const START: u32 = 7;
            pub static EXTRA: u32 = 3;
            pub type Word = u32;
            pub mod inner { pub fn start() -> u32 { super::START } }
        }
        type Variadic = unsafe extern "C" fn(u32, ...) -> u32;
        assert_eq!(size_of::<Variadic>(), size_of::<usize>());
        fn borrow<'a>(value: &'a u32) -> &'a u32 { value }
        let object: &dyn Measure<Output = u32> = &Number(nested::inner::start());
        let value: nested::Word = *borrow(&object.get()) + nested::EXTRA;
        let shifted = ((value << 2) >> 1) ^ 3;
        let chosen = if true && value != 0 { State::Present(shifted) } else { State::Empty };
        let result = match chosen { State::Present(ref n) => *n, State::Empty => return };
        let bits = Bits { value: result };
        let bytes = unsafe { Bits { bytes: result.to_ne_bytes() }.value };
        assert_eq!(unsafe { bits.value }, bytes);
        let step = |n: u32| n + 1;
        let alternate = false || step(result) == 24;
        assert!(alternate);
        macro_rules! keyword_tokens {
            (priv abstract final override virtual typeof become unsized macro do box yield try ~ <-) => { 17u32 };
        }
        macro_rules! units { (42units) => { 42u16 }; }
        macro_rules! duplicate { ($value:expr) => { ($value, $value) }; }
        assert_eq!(duplicate!(value), (10, 10));
        assert_eq!(units!(42units), 42u16);
        let code = keyword_tokens!(priv abstract final override virtual typeof become unsized macro do box yield try ~ <-);
        (step(result), code)
    });
    assert_eq!(answer, (24, 17));
}
