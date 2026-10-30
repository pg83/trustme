//@ compile-fail: cannot be evaluated at compile-time
#![feature(const_precise_live_drops)]
struct Ticket;
impl Drop for Ticket { fn drop(&mut self) {} }
const VALUE:usize={ let _pair=(None::<Ticket>,(Some(Ticket),3u8)); 17 };
fn main() { let _=VALUE; }
