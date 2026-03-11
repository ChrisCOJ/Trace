# Trace

Trace is a wrist-worn embedded task assistance prototype designed to support hospitality staff through dynamic task scheduling, context-aware reminders, and table-state tracking.

The system runs on an ESP32-S3 embedded platform with a touch display and provides wait staff with a lightweight, wearable interface that prioritises service actions in real time.

---

## Project Aim

The project investigates whether a wearable embedded assistant can improve task awareness and reduce missed service actions in hospitality environments by combining:

- table-centred workflow
- dynamic task priority
- embedded user interaction
- context-sensitive reminders

Rather than functioning as a simple checklist, Trace continuously evaluates active service tasks and recommends what should be done next based on urgency and system state.

---

## Core Features

### Table Finite State Machine (FSM)

Each table is modelled independently through a finite state machine representing the service lifecycle:

- Idle
- Serve Water
- Take Order
- Prepare Order
- Serve Order
- Monitor Table
- Present Bill
- Clear Table

State transitions occur through:

- user actions
- external events
- scheduler-triggered task admission

---

### Dynamic Scheduler

A custom scheduler continuously ranks active tasks using a utility score:

utility = base_priority + urgency + age - ignore_penalty

Scheduler inputs include:

- task base priority
- elapsed waiting time
- urgency escalation
- user ignore behaviour

This allows the system to dynamically switch recommended tasks when service conditions change.

---

### Embedded User Interface

Custom low-level display rendering is implemented directly without high-level GUI frameworks.

UI includes:

- current recommended task display
- action buttons
- table overview grid
- task acknowledgement controls

Rendering features include:

- custom 5x7 bitmap font
- rounded rectangle primitives
- touch input decoding
- manual layout system

---

### Haptic Notifications

A DRV2605L haptic driver is integrated to provide tactile notifications for:

- task switching
- urgent reminders
- system alerts

Distinct haptic patterns are mapped to scheduler events.

---

### External Event Injection

The prototype supports external event injection from a mock POS system.

Supported external events include:

- table seated
- order ready
- customers leaving

These events can trigger task admission independently of user interaction.

---

## Hardware Platform

Main platform:

- ESP32-S3 development board
- 1.69" IPS LCD touch display (240x280)
- CST816S touch controller

Additional peripherals:

- DRV2605L haptic driver
- vibration motor
- optional IMU sensing expansion

---

## Software Architecture

/src contains:

- scheduler logic
- FSM engine
- UI rendering
- touch input
- display driver utilities
- system integration layer

Key modules:

- trace_scheduler.c
- table_fsm.c
- trace_system.c
- user_interface.c
- display_util.c
- touch_controller_util.c

---

## Design Philosophy

Trace is intentionally built from low-level components rather than relying on existing smartwatch frameworks.

The objective is to demonstrate:

- embedded systems engineering
- custom scheduling logic
- interaction design under hardware constraints

---

## Current Status

Prototype stage:

- core scheduler operational
- FSM integrated
- UI functional
- haptic feedback integrated

Remaining work:

- scheduler tuning
- evaluation trials
- enclosure refinement

---

## Repository Purpose

This repository documents the technical implementation of the Trace prototype.

---

## Author

Catalin Cojocaru