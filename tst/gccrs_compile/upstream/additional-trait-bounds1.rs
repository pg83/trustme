
use std::marker::{Send, Sync};

trait A {}

impl dyn A + Send {}
impl dyn A + Send + Sync {}
