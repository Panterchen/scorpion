(define (domain toy-car-painting)
  (:requirements :typing :adl :conditional-effects)
  (:types car color state)

  (:constants
    car1 car2 - car
    red blue none - color
    wet dry - state
    idle painting_car1 painting_car2 drying_car1 drying_car2 - state
    yes no - state
  )

  (:predicates
    (car-color ?c - car ?col - color)
    (car-dryness ?c - car ?s - state)
    (station-state ?s - state)
    (disturbance ?d - state)
  )

  ;; ---- Paint Car 1 ----
  (:action start-paint-car1
    :parameters (?col - color)
    :precondition (and
      (station-state idle)
      (car-color car1 none)
    )
    :effect (and
      (not (station-state idle))
      (station-state painting_car1)
    )
  )

  (:action finish-paint-car1
    :parameters (?col - color)
    :precondition (station-state painting_car1)
    :effect (and
      (when (disturbance no)
        (and
          (not (car-color car1 none))
          (car-color car1 ?col)
          (car-dryness car1 wet)
        )
      )
      (not (station-state painting_car1))
      (station-state idle)
    )
  )

  ;; ---- Paint Car 2 ----
  (:action start-paint-car2
    :parameters (?col - color)
    :precondition (and
      (station-state idle)
      (car-color car2 none)
    )
    :effect (and
      (not (station-state idle))
      (station-state painting_car2)
    )
  )

  (:action finish-paint-car2
    :parameters (?col - color)
    :precondition (station-state painting_car2)
    :effect (and
      (when (disturbance no)
        (and
          (not (car-color car2 none))
          (car-color car2 ?col)
          (car-dryness car2 wet)
        )
      )
      (not (station-state painting_car2))
      (station-state idle)
    )
  )

  ;; ---- Dry Car 1 ----
  (:action start-dry-car1
    :parameters ()
    :precondition (and
      (station-state idle)
      (not (car-color car1 none))
      (car-dryness car1 wet)
    )
    :effect (and
      (not (station-state idle))
      (station-state drying_car1)
    )
  )

  (:action finish-dry-car1
    :parameters ()
    :precondition (station-state drying_car1)
    :effect (and
      (when (disturbance no)
        (and
          (not (car-dryness car1 wet))
          (car-dryness car1 dry)
        )
      )
      (not (station-state drying_car1))
      (station-state idle)
    )
  )

  ;; ---- Dry Car 2 ----
  (:action start-dry-car2
    :parameters ()
    :precondition (and
      (station-state idle)
      (not (car-color car2 none))
      (car-dryness car2 wet)
    )
    :effect (and
      (not (station-state idle))
      (station-state drying_car2)
    )
  )

  (:action finish-dry-car2
    :parameters ()
    :precondition (station-state drying_car2)
    :effect (and
      (when (disturbance no)
        (and
          (not (car-dryness car2 wet))
          (car-dryness car2 dry)
        )
      )
      (not (station-state drying_car2))
      (station-state idle)
    )
  )

  ;; ---- Disturbance Management ----
  (:action cause-disturbance
    :parameters ()
    :precondition ()
    :effect (and
      (when (not (car-color car1 none)) (car-dryness car1 wet))
      (when (not (car-color car2 none)) (car-dryness car2 wet))
      (not (disturbance no))
      (disturbance yes)
    )
  )

  (:action resolve-disturbance
    :parameters ()
    :precondition ()
    :effect (and
      (not (disturbance yes))
      (disturbance no)
    )
  )
)
