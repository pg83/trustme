// Extracted from library/core/src/fmt/mod.rs:878
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::fmt;
        
        struct Position {
            longitude: f32,
            latitude: f32,
        }
        
        impl fmt::Debug for Position {
            fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
                f.debug_tuple("")
                 .field(&self.longitude)
                 .field(&self.latitude)
                 .finish()
            }
        }
        
        let position = Position { longitude: 1.987, latitude: 2.983 };
        assert_eq!(format!("{position:?}"), "(1.987, 2.983)");
        
        assert_eq!(format!("{position:#?}"), "(
            1.987,
            2.983,
        )");
        Ok(())
    }
    doctest().unwrap();
}
