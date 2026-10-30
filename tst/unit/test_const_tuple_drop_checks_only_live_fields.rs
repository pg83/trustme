#![feature(const_precise_live_drops)]
use std::sync::atomic::{AtomicUsize,Ordering};
static DROPS:AtomicUsize=AtomicUsize::new(0);
struct Ticket(u8);
impl Drop for Ticket { fn drop(&mut self) { DROPS.fetch_add(self.0 as usize,Ordering::SeqCst); } }
const VACANT:usize={ let _pair=(None::<Ticket>,(None::<Ticket>,3u8)); 17 };
const CAPTURE_FREE:usize={ let _closure=||7u8; 29 };
fn main() {
 assert_eq!((VACANT,CAPTURE_FREE),(17,29));
 assert_eq!(DROPS.load(Ordering::SeqCst),0);
 { let _pair=(Some(Ticket(3)),(Some(Ticket(5)),7u8)); }
 assert_eq!(DROPS.load(Ordering::SeqCst),8);
}
