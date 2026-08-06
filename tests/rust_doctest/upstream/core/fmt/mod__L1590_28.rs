// Extracted from library/core/src/fmt/mod.rs:1590
#![allow(unused)]
fn main() {
    use std::fmt;
    
    struct Foo { nb: i32 }
    
    impl Foo {
        fn new(nb: i32) -> Foo {
            Foo {
                nb,
            }
        }
    }
    
    impl fmt::Display for Foo {
        fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
            // We need to remove "-" from the number output.
            let tmp = self.nb.abs().to_string();
    
            formatter.pad_integral(self.nb >= 0, "Foo ", &tmp)
        }
    }
    
    assert_eq!(format!("{}", Foo::new(2)), "2");
    assert_eq!(format!("{}", Foo::new(-1)), "-1");
    assert_eq!(format!("{}", Foo::new(0)), "0");
    assert_eq!(format!("{:#}", Foo::new(-1)), "-Foo 1");
    assert_eq!(format!("{:0>#8}", Foo::new(-1)), "00-Foo 1");
}
