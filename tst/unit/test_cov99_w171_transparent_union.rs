#![feature(transparent_unions)]

#[repr(transparent)]
union TransparentWord {
    word: u32,
}

fn main() {
    let value = TransparentWord { word: 171 };
    assert_eq!(unsafe { value.word }, 171);
}
