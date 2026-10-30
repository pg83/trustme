use std::cell::RefCell;
fn mark(log:&RefCell<Vec<u16>>,value:u16) -> u16 { log.borrow_mut().push(value); value }
macro_rules! rows {
 ($([$($value:expr),* $(,)?]);* $(;)?) => {{
  let mut result=Vec::<Vec<u16>>::new();
  $(result.push(vec![$($value),*]);)*
  result
 }};
}
macro_rules! forward { ($($tokens:tt)*) => { rows!($($tokens)*) }; }
macro_rules! twice { ($value:expr) => { ($value,$value) }; }
fn main() {
 let log=RefCell::new(Vec::new());
 let values=forward!([mark(&log,7),mark(&log,11),];[];[mark(&log,13)];);
 assert_eq!(values,vec![vec![7,11],vec![],vec![13]]);
 assert_eq!(&*log.borrow(),&[7,11,13]);
 assert!(rows!().is_empty());
 assert_eq!(twice!(mark(&log,17)),(17,17));
 assert_eq!(&*log.borrow(),&[7,11,13,17,17]);
}
