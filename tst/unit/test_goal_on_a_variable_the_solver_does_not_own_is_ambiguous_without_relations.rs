//@ run-pass
//@ aux-build: join_context_aux.rs
// rayon 1.12 `bridge_producer_consumer::helper` under rayon-core's `join_context`: the
// `U` of `impl<T, U, C, F> Consumer<T> for MapConsumer<C, F>` is decided only by
// `F: Fn(T) -> U`.  A nested goal met it as a variable the solver no longer owned,
// made rigid, and `U: Send` was answered by the one generic impl head it could
// take (`Send for &T`), so `ListVecConsumer`'s `Result` became `LinkedList<Vec<&_>>`.
// Upstream: a goal whose self is an unresolved variable is ambiguous outright.
extern crate join_context_aux;

use join_context_aux::{join_context, FnContext};
use std::collections::LinkedList;

pub trait Consumer<Item>: Send + Sized {
    type Result: Send;
    fn consume(self, item: Item) -> Self::Result;
}

pub struct ListVecConsumer;

impl<T: Send> Consumer<T> for ListVecConsumer {
    type Result = LinkedList<Vec<T>>;
    fn consume(self, item: T) -> LinkedList<Vec<T>> {
        let mut list = LinkedList::new();
        list.push_back(vec![item]);
        list
    }
}

pub struct WhileSomeConsumer<C>(C);

impl<T, C> Consumer<Option<T>> for WhileSomeConsumer<C>
where
    C: Consumer<T>,
    T: Send,
{
    type Result = C::Result;
    fn consume(self, item: Option<T>) -> C::Result {
        self.0.consume(item.unwrap())
    }
}

pub struct InspectConsumer<C, F>(C, F);

impl<T, C, F> Consumer<T> for InspectConsumer<C, F>
where
    C: Consumer<T>,
    F: Fn(&T) + Sync + Send,
{
    type Result = C::Result;
    fn consume(self, item: T) -> C::Result {
        (self.1)(&item);
        self.0.consume(item)
    }
}

pub struct MapConsumer<C, F>(C, F);

impl<T, U, C, F> Consumer<T> for MapConsumer<C, F>
where
    C: Consumer<U>,
    F: Fn(T) -> U + Sync + Send,
{
    type Result = C::Result;
    fn consume(self, item: T) -> C::Result {
        self.0.consume((self.1)(item))
    }
}

pub trait Producer: Send + Sized {
    type Item;
    fn split_at(self, index: usize) -> (Self, Self);
    fn first(&self) -> Self::Item;
}

pub struct IterProducer<T>(T, T);

impl<T: Send + Copy> Producer for IterProducer<T> {
    type Item = T;
    fn split_at(self, _index: usize) -> (Self, Self) {
        (IterProducer(self.0, self.1), IterProducer(self.0, self.1))
    }
    fn first(&self) -> T {
        self.0
    }
}

fn helper<P, C>(len: usize, migrated: bool, producer: P, consumer: C) -> C::Result
where
    P: Producer,
    C: Consumer<P::Item> + Clone,
{
    if len <= 1 || migrated {
        return consumer.consume(producer.first());
    }
    let mid = len / 2;
    let (left_producer, right_producer) = producer.split_at(mid);
    let (left_consumer, right_consumer) = (consumer.clone(), consumer);
    let (left_result, _right_result) = join_context(
        |context: FnContext| helper(mid, context.migrated(), left_producer, left_consumer),
        |context: FnContext| helper(len - mid, context.migrated(), right_producer, right_consumer),
    );
    left_result
}

impl Clone for ListVecConsumer {
    fn clone(&self) -> Self { ListVecConsumer }
}
impl<C: Clone> Clone for WhileSomeConsumer<C> {
    fn clone(&self) -> Self { WhileSomeConsumer(self.0.clone()) }
}
impl<C: Clone, F: Clone> Clone for InspectConsumer<C, F> {
    fn clone(&self) -> Self { InspectConsumer(self.0.clone(), self.1.clone()) }
}
impl<C: Clone, F: Clone> Clone for MapConsumer<C, F> {
    fn clone(&self) -> Self { MapConsumer(self.0.clone(), self.1.clone()) }
}

fn check<T: std::fmt::Debug>(x: &Option<T>) {
    assert!(x.is_some());
}

fn main() {
    let consumer = MapConsumer(InspectConsumer(WhileSomeConsumer(ListVecConsumer), check::<i32>), Some::<i32>);
    let result = helper(4, false, IterProducer(5i32, 9i32), consumer);
    assert_eq!(result.len(), 1);
    assert_eq!(result.front().unwrap(), &vec![5]);
}
