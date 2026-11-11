trait Description {
    type Tail: ?Sized;
}

struct First;
struct Second;

impl Description for First {
    type Tail = u32;
}

impl Description for Second {
    type Tail = u32;
}

struct Packet<D: Description> {
    tag: u8,
    tail: D::Tail,
}

fn packet<D: Description>(tail: D::Tail) -> Packet<D>
where
    D::Tail: Sized,
{
    Packet { tag: 11, tail }
}

fn main() {
    let unresolved = packet(std::hint::black_box(29u32));
    let first: Packet<First> = unresolved;
    assert_eq!(first.tag as u32 + first.tail, 40);

    let unresolved = packet(std::hint::black_box(43u32));
    let second: Packet<Second> = unresolved;
    assert_eq!(second.tag as u32 + second.tail, 54);
}
