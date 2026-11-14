//@ crate-type: lib
//@ compile-flags: --emit=metadata
//@ aux-build: cov99_w415_dependent_packet.rs
#![feature(core_intrinsics, generic_const_exprs)]
#![allow(internal_features, incomplete_features)]

extern crate cov99_w415_dependent_packet;

pub const VALID: () = unsafe {
    core::intrinsics::assert_inhabited::<cov99_w415_dependent_packet::Packet<2>>()
};
