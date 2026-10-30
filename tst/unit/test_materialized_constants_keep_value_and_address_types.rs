trait Store { const DATA:[u16;3]; const STEP:fn(u16)->u16; }
struct Input;
fn step(value:u16) -> u16 { value+5 }
impl Store for Input { const DATA:[u16;3]=[13,17,19]; const STEP:fn(u16)->u16=step; }
const ARRAY:&[u16;3]=&Input::DATA;
const FIELD:&u16=&Input::DATA[1];
const NESTED:&&[u16;3]=&&Input::DATA;
const CALLBACK:&fn(u16)->u16=&Input::STEP;
#[inline(never)]
fn read<S: Store>() -> u16 {
 let values=&S::DATA;
 let function=&S::STEP;
 function(values[0])+values[2]
}
union Word { whole:u16, bytes:[u8;2] }
const WORD:Word=Word{whole:0x1234};
#[derive(Debug,PartialEq)]
enum Choice { Empty, Value(u16) }
const CHOICE:Choice=Choice::Value(29);
const EMPTY:Choice=Choice::Empty;
const PAIR:(u16,bool)=(31,true);
const POINTER:*const u16=&Input::DATA[2];
fn main() {
 assert_eq!(unsafe { WORD.bytes },0x1234u16.to_ne_bytes());
 assert_eq!(CHOICE,Choice::Value(29));
 assert_eq!(EMPTY,Choice::Empty);
 assert_eq!(PAIR,(31,true));
 assert_eq!(unsafe { *POINTER },19);
 assert_eq!(read::<Input>(),37);
 assert_eq!((ARRAY[0],*FIELD,NESTED[2]),(13,17,19));
 assert_eq!(CALLBACK(23),28);
 let pointer=ARRAY as *const [u16;3];
 assert_eq!(unsafe {(*pointer)[2]},19);
}
