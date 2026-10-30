//@ aux-build: region_read.rs
use region_read::{Input,Read};
type View<'a> = dyn Read<'a,Output=&'a u16> + 'a;
#[inline(never)]
fn choose<'a,'b>(left: &View<'a>, right: &View<'b>, first: bool) -> u16 where 'a: 'b, 'b: 'a {
 let selected = if first { left } else { right };
 *selected.read()
}
fn main() {
 let first=19u16;
 let second=31u16;
 let left=Input(&first);
 let right=Input(&second);
 assert_eq!(choose(&left,&right,true),19);
 assert_eq!(choose(&left,&right,false),31);
}
