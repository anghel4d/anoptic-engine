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

/-- Endpoints name one shared channel; neither endpoint contains queue state. -/
structure Producer (Value : Type u) (capacity : Nat) where
  channelIdentity : Nat

structure Consumer (Value : Type u) (capacity : Nat) where
  channelIdentity : Nat

structure Endpoints (Value : Type u) (capacity : Nat) where
  producer : Producer Value capacity
  consumer : Consumer Value capacity
  sameChannel : producer.channelIdentity = consumer.channelIdentity

def endpoints (Value : Type u) (capacity identity : Nat) :
    Endpoints Value capacity :=
  ⟨⟨identity⟩, ⟨identity⟩, rfl⟩

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

def sendFrom (_producer : Producer Value capacity)
    (state : Channel Value) (capacityMatches : state.capacity = capacity)
    (value : Value) : Outcome.Result (Channel Value) SendError := by
  subst capacity
  exact send state value

def receiveFrom (_consumer : Consumer Value capacity)
    (state : Channel Value) (capacityMatches : state.capacity = capacity) :
    Option (Value × Channel Value) := by
  subst capacity
  exact receive state

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

def Publication.bimap (leftMap : LeftA → LeftB) (rightMap : RightA → RightB)
    (publication : Publication LeftA RightA) : Publication LeftB RightB :=
  ⟨publication.generation, leftMap publication.left, rightMap publication.right⟩

@[simp] theorem Publication.bimap_id
    (publication : Publication Left Right) :
    publication.bimap id id = publication := by
  cases publication
  rfl

theorem Publication.bimap_comp
    (firstLeft : LeftA → LeftB) (secondLeft : LeftB → LeftC)
    (firstRight : RightA → RightB) (secondRight : RightB → RightC)
    (publication : Publication LeftA RightA) :
    (publication.bimap firstLeft firstRight).bimap secondLeft secondRight =
      publication.bimap (secondLeft ∘ firstLeft) (secondRight ∘ firstRight) := by
  cases publication
  rfl

@[simp] theorem Publication.bimap_preserves_generation
    (leftMap : LeftA → LeftB) (rightMap : RightA → RightB)
    (publication : Publication LeftA RightA) :
    (publication.bimap leftMap rightMap).generation = publication.generation :=
  rfl

def observeLeft (publication : Publication Left Right) : Nat × Left :=
  (publication.generation, publication.left)

def observeRight (publication : Publication Left Right) : Nat × Right :=
  (publication.generation, publication.right)

theorem publication_generations_agree (publication : Publication Left Right) :
    (observeLeft publication).1 = (observeRight publication).1 :=
  rfl

end Transport
end Anoptic
