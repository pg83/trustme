
mod a {}

mod b {
    pub struct X;

    mod c {
        use crate::{a::*, b::*};

        type Y = X;
    }
}
