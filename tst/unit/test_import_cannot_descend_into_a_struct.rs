//@ compile-fail: Unexpected item type Struct in import
mod items { pub struct Record; }
use crate::items::Record::Missing;
fn main() {}
