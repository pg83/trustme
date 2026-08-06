// Extracted from library/core/src/ops/bit.rs:733
#![allow(unused)]
fn main() {
    use std::ops::BitOrAssign;
    
    #[derive(Debug, PartialEq)]
    struct PersonalPreferences {
        likes_cats: bool,
        likes_dogs: bool,
    }
    
    impl BitOrAssign for PersonalPreferences {
        fn bitor_assign(&mut self, rhs: Self) {
            self.likes_cats |= rhs.likes_cats;
            self.likes_dogs |= rhs.likes_dogs;
        }
    }
    
    let mut prefs = PersonalPreferences { likes_cats: true, likes_dogs: false };
    prefs |= PersonalPreferences { likes_cats: false, likes_dogs: true };
    assert_eq!(prefs, PersonalPreferences { likes_cats: true, likes_dogs: true });
}
