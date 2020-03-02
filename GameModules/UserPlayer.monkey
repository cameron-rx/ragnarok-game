Import mojo
Import BasicLibrary
Import Items
Import Enemies

Class Player Extends GameObject
	Field speed:Int
	Field collisionBox:CollisionBox
	Field animState:String = "RIGHT"
	Field idleR:Animation
	Field idleL:Animation
	Field right:Animation
	Field left:Animation
	Field movementVec:Vec2D = New Vec2D(0, 0)
	Field maxHealth:Int
	Field curHealth:Int
	Field heartEmptySprite:Image
	Field heartFullSprite:Image
	Field itemArray:Interactables[2]
	Field curWeapon:WeaponInterface
	Field centreX:Float
	Field centreY:Float
	Field uberCD:Int = 500
	Field uber:Bool = False
	Field uberTime:Int 
	Field hurtSound:Sound
		
	Method New(_x:Float,_y:Float,_speed:Int,_width:Int,_height:Int)
		x = _x
		y = _y
		speed = _speed
		width = _width
		height = _height
		idleR = New Animation("characters/characterIdleRight.png", 16, 24, 4, 100, 2, 2)
		idleL = New Animation("characters/characterIdleLeft.png", 16, 24, 4, 100, 2, 2)
		right = New Animation("characters/characterRunRight.png", 16, 24, 4, 100, 2, 2)
		left = New Animation("characters/characterRunLeft.png", 16, 24, 4, 100, 2, 2)
		heartEmptySprite = LoadImage("gui/ui_heart_empty.png",1)
		heartFullSprite = LoadImage("gui/ui_heart_full.png",1)
		maxHealth = 3
		curHealth = 3
		hurtSound = LoadSound("soundfx/hurt.wav")
	End Method
	
	Method Attack(target:List<Enemy>)
		If curWeapon <> Null Then
			curWeapon.Attack(x, y, target)
		EndIf
	End
	
	Method Hurt()
		If curHealth > 0 And uber = False Then
			curHealth -= 1
			uber = True
			uberTime = Millisecs() + uberCD
			PlaySound(hurtSound,0,0)
		End If
	End Method
	
	Method Heal()
		If curHealth < maxHealth
		curHealth += 1
		End If
	End
	
	Method UseItem1(player:Player)
		If KeyHit(KEY_Q) and itemArray[0] <> Null Then
			itemArray[0].Use(player)
			itemArray[0] = Null
		Endif
		If KeyHit(KEY_E) And itemArray[1] <> Null Then
			itemArray[1].Use(player)
			itemArray[1] = Null
		End If 
	End
	
	Method Update()
		movementVec.Reset()
		If KeyDown(KEY_W)
			movementVec.UpdateY(-1)
		EndIf
		If KeyDown(KEY_S)
			movementVec.UpdateY(1)
		EndIf
		If KeyDown(KEY_A)
			movementVec.UpdateX(-1)
			animState = "LEFT"
		Endif
		If KeyDown(KEY_D)
			movementVec.UpdateX(1)
			animState = "RIGHT"
		Endif
		movementVec.Normalise()
		
		
		
		
		If Millisecs() >= uberTime And uber = True Then
			uber = False
		End If 
			
	End
	
	Method UpdateX()
		If movementVec.GetMagnitude() <> 0
			x += (movementVec.GetX() * speed)
		End If
		centreX = x + 16
	End
	
	Method UpdateY()
		If movementVec.GetMagnitude() <> 0
			y += (movementVec.GetY() * speed)
		Endif
		centreY = y + 30
	End
	
	Method GetX:Float()
		Return (x + 6)
	End
	
	Method GetY:Float()
		Return (y + 18)
	End
	
	Method CollRespX()
		If movementVec.GetMagnitude() <> 0
			x += - (movementVec.GetX() * speed)
		End
	End
	
	Method CollRespY()
		If movementVec.GetMagnitude() <> 0
			y += -(movementVec.GetY() * speed)
		End If
	End Method
	
	Method Draw()
		If uber = True Then
			SetColor(255,0,0)
		Else 
			SetColor(255,255,255)
		End If 
		Select animState
			Case "RIGHT"
				If movementVec.GetX() = "NaN" And movementVec.GetY() = "NaN" Then
					idleR.Play(x, y)
				Else
					right.Play(x, y)
				EndIf
			Case "LEFT"
				If movementVec.GetX() = "NaN" And movementVec.GetY() = "NaN" Then
					idleL.Play(x, y)
				Else
					left.Play(x, y)
				EndIf
		End Select
		
		SetColor(255,255,255)
		
		For Local i = 0 To maxHealth - 1
			DrawImage(heartEmptySprite,500 + (i*48) ,400,0,2,2)
		Next
		For Local i = 0 To curHealth -1
			DrawImage(heartFullSprite,500 + (i*48) ,400,0,2,2)
		Next
		If curWeapon <> Null Then
			curWeapon.Draw(x, y)
		End If
		For Local i = 0 To itemArray.Length - 1
			If itemArray[i] <> Null
				itemArray[i].Draw(i)
			EndIf
		Next

	End Method
	
	Method SetX(_x:Float)
		x = _x
	End

	Method SetY(_y:Float)
		y = _y
	End
		
	Method GetSize()
		Return size
	End
	
	Method GetWidth:Int()
		Return 10
	End Method
	
	Method GetHeight:Int()
		Return 15
	End Method
	
	Method GetCentreX:Float()
		Return centreX
	End Method
	
	Method GetCentreY:Float()
		Return centreY
	End Method
	
	
	Method GetMaxHealth:Int()
		Return maxHealth
	End Method
	
	Method GetCurHealth:Int()
		Return curHealth
	End Method
	
	Method AddItem(item:Interactables)
		If itemArray[0] = Null Then
			itemArray[0] = item
		Else If itemArray[1] = Null Then
			itemArray[1] = item
		End If
	End Method
	
	Method AddWeapon(weapon:WeaponInterface)
		curWeapon = weapon
	End
	
	Method Reset()
		x = 200
		y = 200
		curHealth = 3
		maxHealth = 3
		curWeapon = Null
		itemArray[0] = Null
		itemArray[1] = Null
	End Method
End Class
