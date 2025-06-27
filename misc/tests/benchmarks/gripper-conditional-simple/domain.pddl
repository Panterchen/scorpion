(define (domain gripper-conditional-simple)

   (:predicates (room ?r)
   		(ball ?b)
   		(gripper ?g)
   		(at-robby ?r)
   		(at ?b ?r)
   		(free ?g)
   		(carry ?o ?g)
   		(different ?r1 ?r2))

   (:action move
        :parameters (?r1 ?r2)
        :precondition (and (room ?r1) (room ?r2) (different ?r1 ?r2))
        :effect (and
                (when (at-robby ?r1)
                        (and
                            (not (at-robby ?r1))
                            (at-robby ?r2)
                        )
                )
                (when  (at-robby ?r2)
                        (and
                            (not (at-robby ?r2))
                            (at-robby ?r1)
                        )
                )
        )
   )

   (:action pick
          :parameters (?obj ?room ?gripper)
          :precondition  (and  (ball ?obj) (room ?room) (gripper ?gripper)
   			    (at ?obj ?room) (at-robby ?room) (free ?gripper))
          :effect (and (carry ?obj ?gripper)
   		    (not (at ?obj ?room))
   		    (not (free ?gripper))
   		  )
   )

   (:action drop
       :parameters  (?obj  ?r1 ?r2 ?gripper)
       :precondition  (and  (ball ?obj) (room ?r1) (room ?r2) (gripper ?gripper)
			    (carry ?obj ?gripper) (different ?r1 ?r2))
       :effect (and (free ?gripper)
                    (not (carry ?obj ?gripper))
                    (when (at-robby ?r1)
                        (and
                            (at ?obj ?r1)
                        )
                    )
                    (when (at-robby ?r2)
                        (and
                            (at ?obj ?r2)
                        )
                    )
	    )
    )
)

