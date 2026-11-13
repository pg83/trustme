static LETTER: char = 'R';
static FLAG: bool = true;
static SIGNED: i16 = 23;
static FLOAT: f32 = 2.5;
static TEXT: &str = "scalar";

fn main() {
    let letter = LETTER;
    let code = letter as u32;
    let byte = letter as u8;
    let flag = FLAG as bool;
    let signed = SIGNED;
    let float = FLOAT;
    assert_eq!(!code, 0xffff_ffad);
    assert_eq!(!byte, 173);
    assert!(!(!flag));
    assert_eq!(!signed, -24);
    assert_eq!(-signed, -23);
    assert_eq!(-float, -2.5);
    assert_eq!(TEXT, "scalar");
}
