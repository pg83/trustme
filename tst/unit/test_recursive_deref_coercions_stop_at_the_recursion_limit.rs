//@ compile-fail: Reached the recursion limit
#![recursion_limit = "8"]
use std::ops::Deref;
struct Left;
struct Right;
static LEFT: Left = Left;
static RIGHT: Right = Right;
impl Deref for Left { type Target = Right; fn deref(&self) -> &Right { &RIGHT } }
impl Deref for Right { type Target = Left; fn deref(&self) -> &Left { &LEFT } }
fn need(_: &u32) {}
fn main() { need(&LEFT); }
