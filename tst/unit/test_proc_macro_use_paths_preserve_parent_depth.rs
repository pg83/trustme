//@ proc-macro-aux-build: precise_capture_tokens.rs
mod source {
 pub struct Packet(pub u16);
 pub fn amount()->u16 { 17 }
 pub mod extra { pub const BONUS:u16=5; }
}
mod parent {
 const LOCAL:u16=9;
 pub mod child {
  #[precise_capture_tokens::keep("usesuper::super::source::{PacketasRootPacket,amount};")]
  use super::super::source::{Packet as RootPacket,amount};
  #[precise_capture_tokens::keep("usesuper::LOCALasFromParent;")]
  use super::LOCAL as FromParent;
  #[precise_capture_tokens::keep("usesuper::super::source::extra::*;")]
  use super::super::source::extra::*;
  pub fn total()->u16 { RootPacket(amount()).0+FromParent+BONUS }
 }
}
fn main() { assert_eq!(parent::child::total(),31); }
