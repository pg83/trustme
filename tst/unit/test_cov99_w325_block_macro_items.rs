macro_rules! install {
    (raw $name:ident) => {
        fn $name() -> u32 { 21 }
    };
    (item $item:item) => { $item };
    (stmt $statement:stmt) => { $statement };
}

fn main() {
    install!(raw outer);
    {
        install!(stmt fn inner() -> u32 { 21 });
        assert_eq!(inner(), 21);
    }
    install!(item fn after() -> u32 { 21 });
    assert_eq!(outer() + after(), 42);
}
