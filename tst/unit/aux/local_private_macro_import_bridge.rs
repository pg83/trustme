macro_rules! hidden_seed { () => { 53u16 }; }
use hidden_seed as hidden_alias;

#[macro_export]
macro_rules! opened_local_seed { () => { 59u16 }; }
