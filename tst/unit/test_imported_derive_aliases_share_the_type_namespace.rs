//@ aux-build: derive_type_aliases.rs
//@ aux-build: derive_type_reexports.rs
use derive_type_aliases::Printable;
use derive_type_reexports::Inspectable;
#[derive(Printable)]
struct Pair { left:u16, right:u16 }
#[derive(Debug)]
struct Control(u16);
#[derive(Inspectable)]
struct Reexport(u16);
fn render<T:Printable>(value:&T) -> String { format!("{:?}",value) }
fn main() {
 assert_eq!(format!("{:?}",Control(7)),"Control(7)");
 assert_eq!(format!("{:?}",Reexport(11)),"Reexport(11)");
 let value=Pair{left:3,right:5};
 assert_eq!(render(&value),"Pair { left: 3, right: 5 }");
 let object:&dyn Printable=&value;
 assert_eq!(format!("{:?}",object),"Pair { left: 3, right: 5 }");
}
