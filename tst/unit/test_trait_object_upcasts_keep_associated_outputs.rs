trait Read { type Output; fn read(&self) -> Self::Output; }
trait Extra: Read { fn extra(&self) -> u16; }
struct Input(u16);
impl Read for Input { type Output=u16; fn read(&self) -> u16 { self.0 } }
impl Extra for Input { fn extra(&self) -> u16 { self.0+1 } }
#[inline(never)]
fn base(value: &dyn Extra<Output=u16>) -> &dyn Read<Output=u16> { value }
#[inline(never)]
fn raw(value: *const dyn Extra<Output=u16>) -> *const dyn Read<Output=u16> { value }
#[inline(never)]
fn erase_send(value: &(dyn Read<Output=u16>+Send)) -> &dyn Read<Output=u16> { value }
fn main() {
 let input=Input(23);
 let extra: &dyn Extra<Output=u16>=&input;
 assert_eq!(extra.extra(),24);
 assert_eq!(base(extra).read(),23);
 let send: &(dyn Read<Output=u16>+Send)=&input;
 assert_eq!(erase_send(send).read(),23);
 let pointer=raw(extra as *const dyn Extra<Output=u16>);
 assert_eq!(unsafe { (*pointer).read() },23);
}
