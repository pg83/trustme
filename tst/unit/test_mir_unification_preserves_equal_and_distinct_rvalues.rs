union Word { whole: u16, parts: [u8;2] }
#[inline(never)]
fn repeats(value:u16, first:bool) -> [u16;3] { if first { [value;3] } else { [value;3] } }
#[inline(never)]
fn negate(value:i16, first:bool) -> i16 { if first { -value } else { -value } }
#[inline(never)]
fn invert(value:u16, first:bool) -> u16 { if first { !value } else { !value } }
#[inline(never)]
fn slice(value:&[u16;3], first:bool) -> &[u16] { if first { value } else { value } }
#[inline(never)]
fn pack(value:u16, first:bool) -> Word { if first { Word { whole:value } } else { Word { whole:value } } }
#[inline(never)]
fn repeat_other(a:u16,b:u16,first:bool) -> [u16;3] { if first { [a;3] } else { [b;3] } }
#[inline(never)]
fn unary_other(value:i16,first:bool) -> i16 { if first { -value } else { !value } }
#[inline(never)]
fn invert_other(a:u16,b:u16,first:bool) -> u16 { if first { !a } else { !b } }
#[inline(never)]
fn slice_other<'a>(left:&'a [u16;3],right:&'a [u16;3],first:bool) -> &'a [u16] { if first { left } else { right } }
#[inline(never)]
fn slice_length<'a>(left:&'a [u16;2],right:&'a [u16;3],first:bool) -> &'a [u16] { if first { left } else { right } }
#[inline(never)]
fn pack_other(a:u16,b:u16,first:bool) -> Word { if first { Word { whole:a } } else { Word { whole:b } } }
#[inline(never)]
fn pack_variant(value:u16,parts:[u8;2],first:bool) -> Word { if first { Word { whole:value } } else { Word { parts } } }
fn main() {
 let value=std::hint::black_box(7u16);
 for first in [true,false] {
  let array=repeats(value,first);
  assert_eq!(array,[7,7,7]);
  let expected=if first { 7u16 } else { 11u16 };
  assert_eq!(repeat_other(value,11,first),[expected;3]);
  assert_eq!(unary_other(value as i16,first),if first {-7} else {!7i16});
  assert_eq!(invert_other(value,11,first),!expected);
  let other=[11u16;3];
  assert_eq!(slice_other(&array,&other,first),&[expected;3]);
  let short=[5u16;2];
  assert_eq!(slice_length(&short,&array,first),if first {&short[..]} else {&array[..]});
  assert_eq!(unsafe { pack_other(value,11,first).whole },expected);
  let bytes=if first {value.to_ne_bytes()} else {[5u8,9]};
  assert_eq!(unsafe { pack_variant(value,[5,9],first).parts },bytes);
  assert_eq!(negate(value as i16,first),-7);
  assert_eq!(invert(value,first),!7u16);
  assert_eq!(slice(&array,first),&[7,7,7]);
  let word=pack(value,first);
  assert_eq!(unsafe { word.whole },7);
  assert_eq!(unsafe { word.parts },7u16.to_ne_bytes());
 }
}
