//@ compile-variant: -O

mod outer {
    const VALUE: u16 = 113;
    pub mod inner {
        const LOCAL: u16 = 127;
        pub fn value() -> u16 {
            let left = { struct Local; let _ = Local; self::LOCAL };
            let right = {
                enum Local { Ready }
                let _ = Local::Ready;
                { struct Nested; let _ = Nested; super::VALUE }
            };
            left + right
        }
    }
}

fn main() { assert_eq!(outer::inner::value(), 240); }
