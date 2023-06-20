(uart-start 57600)

;input str list
(defun strLst2byteLst(lst data)
    (let (
          (p 0)
          (singleChar 0)
         )
         (looprange i 0 (length lst)
            (let ((str (ix lst i)))
                (looprange ii 0 (str-len str)      
                    (progn
                        (setvar `singleChar (bufget-u8 str ii))
                        ;(print (str-from-n p "data index:%d"))
                        ;(print (str-from-n singleChar "singleChar:%c"))
                        (bufset-u8 data p singleChar)
                        (setvar `p (+ p 1))
                    )
                )   
            )
         )
         p
    )
)

;input char list
(defun checksum(data size)
    (progn
    (let (
            (sum 0)
         )
         (progn
            (looprange i 0 size
                (progn 
                    (setvar `sum (+ sum (bufget-u8 data i)))
                    ;(print sum)
                )
            )
            ;(print "total")
            ;(print sum)
            (setvar `sum (mod sum 65536))
            ;(print "after mod")
            ;(print sum)
            (setvar `sum (bitwise-xor sum 65535))
            ;(print "after xor")
            ;(print sum)
            (bufset-u8 data size (bitwise-and 0xff sum))
            ;(print "low checksum")
            ;(print (bitwise-and 0xff sum))
            (setvar `sum (shr sum 8))
            (bufset-u8 data (+ size 1) (bitwise-and 0xff sum))
            ;(print "high checksum")
            ;(print (bitwise-and 0xff sum))
         )
    )
    (+ size 2)
    )
)

(loopwhile t
    (progn
        (def data (array-create 50))
        (def pitch (ix (get-imu-rpy) 1))
        (def speed (get-speed))
        (def current-raw (get-current))
        ;(define #yaw-rate (ix (get-imu-gyro) 2))
        ;(define #yaw (ix (get-imu-rpy) 2))
        (def dataList (list
            (to-str pitch)
            (to-str speed)))
        
        (let (
                (fillSize (strLst2byteLst dataList data))
             )
            (progn
                ;(print (str-from-n  fillSize "data fill size1:%d"))
                (setvar `fillSize (checksum data fillSize))
                ;(print (str-from-n  fillSize "data fill size2:%d"))
                (bufset-u8 data fillSize 0x0a)
                (setvar `fillSize (+ fillSize 1))
                (def finalData (array-create fillSize))
                (bufcpy finalData 0 data 0 fillSize)
                
                ;sending
                (uart-write finalData)
                
                (free finalData)
                (free data)
            )
        )
        (sleep 0.5)
))
