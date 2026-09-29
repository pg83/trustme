// cipher's `ZeroTweak<C>` forwards `f: impl BlockCipherEncClosure<BlockSize = Self::BlockSize>`
// to its inner cipher wrapped as a tweak closure whose block size is `C::BlockSize`.
// The bound's `<ZeroTweak<C> as BlockSizeUser>::BlockSize` is `C::BlockSize` by
// `ZeroTweak`'s impl: rustc normalizes the where-clauses of a body before it
// checks it (`normalize_param_env_or_error`), so the bound meets the goal.
use std::marker::PhantomData;

trait BlockSizeUser {
    type BlockSize;
}
trait TweakSizeUser {
    type TweakSize;
}

trait EncClosure: BlockSizeUser {
    fn call(self) -> u32;
}
trait TweakEncClosure: BlockSizeUser + TweakSizeUser {
    fn call(self) -> u32;
}

trait TweakEncrypt: BlockSizeUser + TweakSizeUser + Sized {
    fn encrypt_with_backend(&self, f: impl TweakEncClosure<BlockSize = Self::BlockSize, TweakSize = Self::TweakSize>) -> u32;
}
trait Encrypt: BlockSizeUser {
    fn encrypt_with_backend(&self, f: impl EncClosure<BlockSize = Self::BlockSize>) -> u32;
}

struct ZeroTweak<C: TweakSizeUser + BlockSizeUser>(C);

impl<C: TweakSizeUser + BlockSizeUser> BlockSizeUser for ZeroTweak<C> {
    type BlockSize = C::BlockSize;
}

impl<C: TweakEncrypt> Encrypt for ZeroTweak<C> {
    fn encrypt_with_backend(&self, f: impl EncClosure<BlockSize = Self::BlockSize>) -> u32 {
        self.0.encrypt_with_backend(Wrapper { f, _pd: PhantomData })
    }
}

struct Wrapper<TS, BS, F> {
    f: F,
    _pd: PhantomData<(TS, BS)>,
}
impl<TS, BS, F> BlockSizeUser for Wrapper<TS, BS, F> {
    type BlockSize = BS;
}
impl<TS, BS, F> TweakSizeUser for Wrapper<TS, BS, F> {
    type TweakSize = TS;
}
impl<TS, BS, F: EncClosure<BlockSize = BS>> TweakEncClosure for Wrapper<TS, BS, F> {
    fn call(self) -> u32 {
        self.f.call() + 1
    }
}

struct Cipher;
impl BlockSizeUser for Cipher {
    type BlockSize = u8;
}
impl TweakSizeUser for Cipher {
    type TweakSize = u16;
}
impl TweakEncrypt for Cipher {
    fn encrypt_with_backend(&self, f: impl TweakEncClosure<BlockSize = u8, TweakSize = u16>) -> u32 {
        f.call() * 10
    }
}

struct Job;
impl BlockSizeUser for Job {
    type BlockSize = u8;
}
impl EncClosure for Job {
    fn call(self) -> u32 {
        4
    }
}

fn main() {
    assert_eq!(ZeroTweak(Cipher).encrypt_with_backend(Job), 50);
}
