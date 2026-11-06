trait Atom {}
trait Stamp { fn stamp(&self) -> u8; }
impl Stamp for dyn Atom { fn stamp(&self) -> u8 { 11 } }
impl Stamp for dyn Atom + Send { fn stamp(&self) -> u8 { 17 } }
impl Stamp for dyn Atom + Sync { fn stamp(&self) -> u8 { 23 } }

struct Value;
impl Atom for Value {}

fn main() {
    let value = Value;
    let plain: &dyn Atom = &value;
    let sent: &(dyn Atom + Send) = &value;
    let shared: &(dyn Atom + Sync) = &value;
    assert_eq!(plain.stamp(), 11);
    assert_eq!(sent.stamp(), 17);
    assert_eq!(shared.stamp(), 23);
}
