#![feature(extern_types)]
extern "C" { type Foreign; }
struct Pair<T = u16>(T, T);
type WordPair = Pair<u16>;
enum Choice<T> { Empty, Value(T) }
union Bits { word: u16, bytes: [u8; 2] }
trait Read { fn read(&self) -> u16; }
impl Read for Pair { fn read(&self) -> u16 { self.0 + self.1 } }
fn opaque(value: *const Foreign) -> *const Foreign { value }
fn main() {
    let pair: WordPair = Pair(61, 67);
    let view: &dyn Read = &pair;
    assert_eq!(view.read(), 128);
    let choice = Choice::Value(pair);
    assert_eq!(match choice { Choice::Value(Pair(a, b)) => a + b, Choice::Empty => 0 }, 128);
    assert!(matches!(Choice::<u16>::Empty, Choice::Empty));
    let bits = Bits { word: 0x0201 };
    assert_eq!(unsafe { bits.word }, 0x0201);
    assert_eq!(unsafe { bits.bytes }, [1, 2]);
    let byte = 157u8;
    let foreign = &byte as *const u8 as *const Foreign;
    assert_eq!(opaque(foreign) as *const u8, &byte as *const u8);
}
