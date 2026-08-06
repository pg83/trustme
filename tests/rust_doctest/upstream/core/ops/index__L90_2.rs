// Extracted from library/core/src/ops/index.rs:90
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::ops::{Index, IndexMut};
        
        #[derive(Debug)]
        enum Side {
            Left,
            Right,
        }
        
        #[derive(Debug, PartialEq)]
        enum Weight {
            Kilogram(f32),
            Pound(f32),
        }
        
        struct Balance {
            pub left: Weight,
            pub right: Weight,
        }
        
        impl Index<Side> for Balance {
            type Output = Weight;
        
            fn index(&self, index: Side) -> &Self::Output {
                println!("Accessing {index:?}-side of balance immutably");
                match index {
                    Side::Left => &self.left,
                    Side::Right => &self.right,
                }
            }
        }
        
        impl IndexMut<Side> for Balance {
            fn index_mut(&mut self, index: Side) -> &mut Self::Output {
                println!("Accessing {index:?}-side of balance mutably");
                match index {
                    Side::Left => &mut self.left,
                    Side::Right => &mut self.right,
                }
            }
        }
        
        let mut balance = Balance {
            right: Weight::Kilogram(2.5),
            left: Weight::Pound(1.5),
        };
        
        // In this case, `balance[Side::Right]` is sugar for
        // `*balance.index(Side::Right)`, since we are only *reading*
        // `balance[Side::Right]`, not writing it.
        assert_eq!(balance[Side::Right], Weight::Kilogram(2.5));
        
        // However, in this case `balance[Side::Left]` is sugar for
        // `*balance.index_mut(Side::Left)`, since we are writing
        // `balance[Side::Left]`.
        balance[Side::Left] = Weight::Kilogram(3.0);
        Ok(())
    }
    doctest().unwrap();
}
