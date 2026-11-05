#![feature(deref_pure_trait)]
use std::marker::PhantomData;
use std::ops::{Deref, DerefPure};

pub struct Lens<T: ?Sized>(u8, PhantomData<T>);
pub fn scalar<T: ?Sized>() -> Lens<T> { Lens(17, PhantomData) }
impl Deref for Lens<u8> { type Target = u8; fn deref(&self) -> &u8 { &self.0 } }
impl Deref for Lens<u16> { type Target = u8; fn deref(&self) -> &u8 { &self.0 } }
unsafe impl DerefPure for Lens<u8> {}
unsafe impl DerefPure for Lens<u16> {}

pub struct PairLens<T: ?Sized>((u8, u8), PhantomData<T>);
pub fn pair<T: ?Sized>() -> PairLens<T> { PairLens((23, 29), PhantomData) }
impl Deref for PairLens<u8> { type Target = (u8, u8); fn deref(&self) -> &(u8, u8) { &self.0 } }
impl Deref for PairLens<u16> { type Target = (u8, u8); fn deref(&self) -> &(u8, u8) { &self.0 } }
unsafe impl DerefPure for PairLens<u8> {}
unsafe impl DerefPure for PairLens<u16> {}
