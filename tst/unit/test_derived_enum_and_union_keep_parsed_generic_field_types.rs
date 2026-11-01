#[derive(Clone, Debug, PartialEq)]
enum Message<T> { Data(T), Code(u16), Empty(()) }
#[derive(Clone, Copy)]
union Word<T: Copy> { payload: T, raw: u32 }
fn main() {
    let original = Message::Data(String::from("payload"));
    assert_eq!(original.clone(), original);
    assert_eq!(Message::<String>::Code(17).clone(), Message::Code(17));
    assert_eq!(Message::<String>::Empty(()).clone(), Message::Empty(()));
    let word = Word { payload: 23u32 };
    assert_eq!(unsafe { word.clone().payload }, 23);
    assert_eq!(unsafe { Word::<u32> { raw: 29 }.clone().raw }, 29);
}
