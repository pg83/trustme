// Extracted from library/core/src/fmt/mod.rs:2552
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::fmt;
        
        struct Arm<'a, L, R>(&'a (L, R));
        struct Table<'a, K, V>(&'a [(K, V)], V);
        
        impl<'a, L, R> fmt::Debug for Arm<'a, L, R>
        where
            L: 'a + fmt::Debug, R: 'a + fmt::Debug
        {
            fn fmt(&self, fmt: &mut fmt::Formatter<'_>) -> fmt::Result {
                L::fmt(&(self.0).0, fmt)?;
                fmt.write_str(" => ")?;
                R::fmt(&(self.0).1, fmt)
            }
        }
        
        impl<'a, K, V> fmt::Debug for Table<'a, K, V>
        where
            K: 'a + fmt::Debug, V: 'a + fmt::Debug
        {
            fn fmt(&self, fmt: &mut fmt::Formatter<'_>) -> fmt::Result {
                fmt.debug_set()
                .entries(self.0.iter().map(Arm))
                .entry(&Arm(&(format_args!("_"), &self.1)))
                .finish()
            }
        }
        Ok(())
    }
    doctest().unwrap();
}
