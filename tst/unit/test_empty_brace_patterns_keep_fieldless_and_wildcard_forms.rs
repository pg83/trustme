struct Unit;
struct Empty();
struct Named {}
struct Pair(u8, u16);
enum Choice { Empty, Tuple(), Named {} }
fn main() {
    let Unit {} = Unit;
    let Empty {} = Empty();
    let Named {} = Named {};
    let Pair { .. } = Pair(3, 5);
    for value in [Choice::Empty, Choice::Tuple(), Choice::Named {}] {
        match value { Choice::Empty {} | Choice::Tuple {} | Choice::Named {} => {} }
    }
}
