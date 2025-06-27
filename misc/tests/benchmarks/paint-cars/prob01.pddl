(define (problem toy-car-assembly-instance)
  (:domain toy-car-painting)

  (:objects )

  (:init
    (car-color car1 none)
    (car-color car2 none)
    (car-dryness car1 wet)
    (car-dryness car2 wet)
    (station-state idle)
    (disturbance no)
  )

  (:goal (and
    (car-color car1 red)
    (car-dryness car1 dry)
    (car-color car2 blue)
    (car-dryness car2 dry)
  ))
)
