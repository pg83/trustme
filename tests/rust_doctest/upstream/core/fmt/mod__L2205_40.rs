// Extracted from library/core/src/fmt/mod.rs:2205
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::fmt;
        use std::net::Ipv4Addr;
        
        struct Foo {
            bar: i32,
            baz: String,
            addr: Ipv4Addr,
        }
        
        impl fmt::Debug for Foo {
            fn fmt(&self, fmt: &mut fmt::Formatter<'_>) -> fmt::Result {
                fmt.debug_struct("Foo")
                    .field("bar", &self.bar)
                    .field("baz", &self.baz)
                    .field("addr", &format_args!("{}", self.addr))
                    .finish()
            }
        }
        
        assert_eq!(
            "Foo { bar: 10, baz: \"Hello World\", addr: 127.0.0.1 }",
            format!("{:?}", Foo {
                bar: 10,
                baz: "Hello World".to_string(),
                addr: Ipv4Addr::new(127, 0, 0, 1),
            })
        );
        Ok(())
    }
    doctest().unwrap();
}
