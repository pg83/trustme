//@ compile-variant: -O

fn keep<T>(left: T, _: T) -> T { left }

fn main() {
    let integer = keep(29, 31);
    let float = keep(2.5, 3.5);
    let reversed = keep(keep(37, 41), integer);
    let integer: u16 = integer;
    let reversed: u16 = reversed;
    let float: f32 = float;
    assert_eq!(integer, 29);
    assert_eq!(reversed, 37);
    assert_eq!(float, 2.5);
}
