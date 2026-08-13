/-
SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors

SPDX-License-Identifier: LGPL-3.0
Anoptic targets ISO C++26.
-/

import Std
import Anoptic.Outcome

namespace Anoptic

universe u v

namespace Transport

inductive SendError where
  | full
  deriving DecidableEq

structure Channel (Value : Type u) where
  capacity : Nat
  items : List Value

/-- Endpoints expose only their half of the channel protocol. -/
structure Producer (Value : Type u) (capacity : Nat) where
  channel : Channel Value
  capacity_matches : channel.capacity = capacity

structure Consumer (Value : Type u) (capacity : Nat) where
  channel : Channel Value
  capacity_matches : channel.capacity = capacity

def empty (Value : Type u) (capacity : Nat) : Channel Value :=
  ⟨capacity, []⟩

def send (channel : Channel Value) (value : Value) :
    Outcome.Result (Channel Value) SendError :=
  if channel.items.length < channel.capacity then
    .ok ⟨channel.capacity, channel.items ++ [value]⟩
  else
    .error .full

def receive (channel : Channel Value) : Option (Value × Channel Value) :=
  match channel.items with
  | [] => none
  | head :: tail => some (head, ⟨channel.capacity, tail⟩)

def sendFrom (producer : Producer Value capacity) (value : Value) :
    Outcome.Result (Producer Value capacity) SendError :=
  if producer.channel.items.length < producer.channel.capacity then
    .ok ⟨⟨producer.channel.capacity, producer.channel.items ++ [value]⟩,
      producer.capacity_matches⟩
  else
    .error .full

def handoff (producer : Producer Value capacity) : Consumer Value capacity :=
  ⟨producer.channel, producer.capacity_matches⟩

def receiveFrom (consumer : Consumer Value capacity) :
    Option (Value × Consumer Value capacity) :=
  match consumer.channel.items with
  | [] => none
  | head :: tail =>
      some (head, ⟨⟨consumer.channel.capacity, tail⟩,
        consumer.capacity_matches⟩)

@[simp] theorem send_empty_succeeds (value : Value) (capacity : Nat)
    (positive : 0 < capacity) :
    send (empty Value capacity) value =
      .ok ⟨capacity, [value]⟩ := by
  simp [send, empty, positive]

@[simp] theorem receive_singleton (value : Value) (capacity : Nat) :
    receive (Channel.mk capacity [value]) =
      some (value, empty Value capacity) :=
  rfl

def map (function : A → B) (channel : Channel A) : Channel B :=
  ⟨channel.capacity, channel.items.map function⟩

@[simp] theorem map_id (channel : Channel A) : map id channel = channel := by
  cases channel
  simp [map]

theorem map_comp (first : A → B) (second : B → C) (channel : Channel A) :
    map second (map first channel) = map (second ∘ first) channel := by
  cases channel
  simp [map, List.map_map]

structure Latest (Value : Type u) where
  generation : Nat
  value : Option Value

def publish (latest : Latest Value) (value : Value) : Latest Value :=
  ⟨latest.generation + 1, some value⟩

@[simp] theorem acquire_published (latest : Latest Value) (value : Value) :
    (publish latest value).value = some value :=
  rfl

structure Lanes (Left : Type u) (Right : Type v) where
  left : Channel Left
  right : Channel Right

def updateLeft (lanes : Lanes Left Right) (left : Channel Left) :
    Lanes Left Right :=
  ⟨left, lanes.right⟩

@[simp] theorem updateLeft_preserves_right (lanes : Lanes Left Right)
    (left : Channel Left) :
    (updateLeft lanes left).right = lanes.right :=
  rfl

/-- Atomic publication carries one generation for all independent lanes. -/
structure Publication (Left : Type u) (Right : Type v) where
  generation : Nat
  left : Left
  right : Right

def observeLeft (publication : Publication Left Right) : Nat × Left :=
  (publication.generation, publication.left)

def observeRight (publication : Publication Left Right) : Nat × Right :=
  (publication.generation, publication.right)

theorem publication_generations_agree (publication : Publication Left Right) :
    (observeLeft publication).1 = (observeRight publication).1 :=
  rfl

end Transport
end Anoptic
