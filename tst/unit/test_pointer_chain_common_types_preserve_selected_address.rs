#[inline(never)]
fn mut_raw(first:bool,value:&mut u16,raw:*mut u16) -> u16 { let pointer=if first { value } else { raw }; unsafe { *(pointer as *const u16) } }
#[inline(never)]
fn raw_mut(first:bool,value:&mut u16,raw:*mut u16) -> u16 { let pointer=if first { raw } else { value }; unsafe { *(pointer as *const u16) } }
#[inline(never)]
fn shared_raw(first:bool,value:&u16,raw:*const u16) -> u16 { let pointer=if first { value } else { raw }; unsafe { *(pointer as *const u16) } }
#[inline(never)]
fn raw_shared(first:bool,value:&u16,raw:*const u16) -> u16 { let pointer=if first { raw } else { value }; unsafe { *(pointer as *const u16) } }
#[inline(never)]
fn raw_forms(first:bool,value:*mut u16,raw:*const u16) -> u16 { let pointer=if first { raw } else { value }; unsafe { *pointer } }
#[inline(never)]
fn borrow_forms(first:bool,value:&mut u16,other:&u16) -> u16 { let pointer=if first { other } else { value }; *pointer }
fn main() {
 let mut left=std::hint::black_box(17u16);
 let mut right=std::hint::black_box(31u16);
 let raw=&mut right as *mut u16;
 for first in [true,false] {
  let a=if first {17} else {31};
  let b=if first {31} else {17};
  assert_eq!(mut_raw(first,&mut left,raw),a);
  assert_eq!(raw_mut(first,&mut left,raw),b);
  assert_eq!(shared_raw(first,&left,raw as *const u16),a);
  assert_eq!(raw_shared(first,&left,raw as *const u16),b);
  assert_eq!(raw_forms(first,&mut left as *mut u16,raw as *const u16),b);
  assert_eq!(borrow_forms(first,&mut left,&right),b);
 }
}
