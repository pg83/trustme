#![feature(extern_types, trivial_bounds)]

extern "C" {
    type Foreign;
}

fn assumed_sized()
where
    Foreign: Sized,
{
    let _value: Foreign;
}

fn main() {
    assert_eq!(
        std::mem::size_of::<*const Foreign>(),
        std::mem::size_of::<usize>(),
    );
}
