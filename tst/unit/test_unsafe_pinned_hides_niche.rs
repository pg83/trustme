#![feature(unsafe_pinned)]

use std::mem::size_of;
use std::num::NonZeroU32;
use std::pin::UnsafePinned;

const _: () = assert!(size_of::<Option<UnsafePinned<NonZeroU32>>>() == 8);

fn main() {}
