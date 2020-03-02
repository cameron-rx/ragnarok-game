Import mojo
Import BasicLibrary
Import UserPlayer

Class Enemy
	Field type:Int
	Field maxHealth:Float
	Field curHealth:Float
	Field speed:Float
	Field x:Float
	Field y:Float
	Field width:Int = 16
	Field height:Int = 24
	Field active:Bool
	Field range:Int 
	Field vecSpeed:Vec2D
	Field idle:Animation
	Field left:Animation
	Field right:Animation
	Field summon:Animation
	Field animState:String = "IDLE"
	Field cd:Int
	Field cdTime:Int
	Field knockback:Bool = False
	Field knockSpeed:Float = -4
	Field chargeTime:Int
	Field charge:Bool = False
	Field playerDist:Int 
	
	Method New(_type:Int, _x:Float, _y:Float, _active:Bool)
		Local scaleX:Float
		Local scaleY:Float
		type = _type
		Select type
			Case 1 'melee unit
				maxHealth = 2
				curHealth = 2
				speed = 1.5
				range = 200
				width = 16
				height = 24
				cd = 2000
				scaleX = 1
				scaleY = 1
			Case 2 'necro
				maxHealth = 3
				curHealth = 3
				speed = 0.5
				range = 300
				width = 16
				height = 20
				cd = 4000
				scaleX = 1.5
				scaleY = 1.5
			Case 3 'archer
			Case 4 'skele
				maxHealth = 1
				curHealth = 1
				speed = 2
				range = 300
				cd = 1000
				width = 16
				height = 16
				scaleX = 1
				scaleY = 1
			Case 10 
				maxHealth = 10
				curHealth = 10
				speed = 1
				range = 500
				cd = 1000
				width = 32
				height = 36
				scaleX = 2
				scaleY = 2
			End
		x = _x
		y = _y
		active = _active
		idle = New Animation("characters/enemies/" + type + "i.png", width, height, 4, 100, scaleX, scaleY)
		right = New Animation("characters/enemies/" + type + "r.png", width, height, 4, 100, scaleX, scaleY)
		left = New Animation("characters/enemies/" + type + "l.png", width, height, 4, 100, scaleX, scaleY)
		summon = New Animation("effects/summon.png",16,20,4,100,scaleX,scaleY)
		vecSpeed = New Vec2D(0,0)
		width *= scaleX
		height *= scaleY
	End Method
	
	Method Draw()
	
		If type = 2 And Millisecs() >= (cdTime - 1500) Then
			summon.Play(x,y)
		End If 
		'Sprite Drawing
		Select animState
			Case "IDLE"
				idle.Play(x,y)
			Case "RIGHT"
				right.Play(x,y)
			Case "LEFT"
				left.Play(x,y)
		End Select

		'Health Drawing
		SetColor(0,0,0)
		DrawRect(x-1,y-8.5,width + 2, 6)
		SetColor(255,255,255)
		DrawRect(x,y-7.5,width,5)
		SetColor(255,0,0)
		Local healthRatio:Float = curHealth/maxHealth
		DrawRect(x,y-7.5,healthRatio*width,5)
		SetColor(255,255,255)
	End Method 
	
	Method CheckActive(playerX:Float,playerY:Float)
		Local xDist:Float = playerX - (x+(width/2))
		Local yDist:Float = playerY - (y+(width/2))
		Local Dist = Sqrt((xDist * xDist) + (yDist * yDist))
		If Dist < range Then
			active = True
		End If
		
		If knockSpeed >= 0 Then
			knockback = False
			knockSpeed = -4
		Else If knockSpeed >= -4 And knockSpeed < 0 And knockback = True
			knockSpeed += 0.2
		End If 
		
		
	End Method
	
	Method CalcMove(playerX:Float,playerY:Float)
		If active = True and knockback = False
			vecSpeed.Reset()
			vecSpeed.UpdateX(playerX+15-(x+(width/2)))
			vecSpeed.UpdateY(playerY+32-(y+(width/2)))
			vecSpeed.Normalise()
		End If
	End Method
	
	Method Damage(ax:Int,ay:Int,aw:Int,ah:Int)
		If Collide(ax,ay,aw,ah,x,y,width,height) Then 
			curHealth -= 1
			knockback = True
		End If 
	End Method 
	
	Method Summon:Bool(player:Player)
		Local centreX = x + 7
		Local centreY = y + 14
		playerDist = findDist(centreX, centreY, player.GetCentreX(), player.GetCentreY())
		If charge = False Then
			chargeTime = Millisecs() + 1000
		End If
		
		If findDist(centreX, centreY, player.GetCentreX(), player.GetCentreY()) < 300 Then
			charge = True
			If (Millisecs() >= cdTime) and Millisecs() >= chargeTime Then
				cdTime = Millisecs + cd
				charge = False
				Return True
			End If
		End If
		If findDist(centreX, centreY, player.GetCentreX(), player.GetCentreY()) > 350 And charge = True
			charge = False
		End If
		Return False
	End
	
	Method Attack(player:Player)
		Local centreX = x + (width/2)
		Local centreY = y+ (height/2)
		playerDist = findDist(centreX, centreY, player.GetCentreX(), player.GetCentreY())
		Select type
			Case 1 'melee unit
				If charge = False Then
					chargeTime = Millisecs() +2000
				End If
				If findDist(centreX, centreY, player.GetCentreX(), player.GetCentreY()) < 40 Then
					charge = True
					If (Millisecs() >= cdTime) And Millisecs() >= chargeTime Then
						cdTime = Millisecs() +cd
						player.Hurt()
						charge = False
					End If
				End If
				If findDist(centreX, centreY, player.GetCentreX(), player.GetCentreY()) > 45 And charge = True
					charge = False
				End If
			Case 2 'necromancer
				
			Case 3 'small skeles

			Case 4 'skele archer
				If charge = False Then
					chargeTime = Millisecs() + 1000
				End If
				If findDist(centreX, centreY, player.GetCentreX(), player.GetCentreY()) < 35 Then
					charge = True
					If (Millisecs() >= cdTime) And Millisecs() >= chargeTime Then
						cdTime = Millisecs() +cd
						player.Hurt()
						charge = False
					End If
				End If
				If findDist(centreX, centreY, player.GetCentreX(), player.GetCentreY()) > 35 And charge = True
					charge = False
				End If
		End
		
		
		
	End Method
	
	Method MoveX()
		If vecSpeed.GetMagnitude <> 0 and knockback = False Then
			x += vecSpeed.GetX() * speed
		End If
		
		If vecSpeed.GetMagnitude <> 0 and knockback = True Then
			x += vecSpeed.GetX() * knockSpeed
		EndIf
		
		If vecSpeed.GetX() > 0 Then
			animState = "RIGHT"
		Else 
			animState = "LEFT"
		End If 
	End Method
	
	Method MoveY()
		If vecSpeed.GetMagnitude <> 0 and knockback = False Then
			y += vecSpeed.GetY() * speed
		End If
		
		If vecSpeed.GetMagnitude <> 0 and knockback = True Then
			y += vecSpeed.GetY() * knockSpeed
		End If
		
	End Method
	
	Method CorrectX()
		If knockback = False Then
			x -= vecSpeed.GetX() * speed
		Else If knockback = True Then
			x -= vecSpeed.GetX() * knockSpeed
		End If 
	End Method
	
	Method CorrectY()
		If knockback = False Then
			y -= vecSpeed.GetY() * speed
		Else If knockback = True Then
			y -= vecSpeed.GetY() * knockSpeed
		End If 
	End Method
	
	Method GetX()
		Return x
	End Method
	
	Method GetY()
		Return y
	End Method
	
	Method GetType:Int()
		Return type
	End
	
	Method GetWidth()
		Return width
	End Method
	
	Method GetHeight()
		Return height
	End Method
	
	Method GetId()
		Return id
	End Method
	
	Method CheckHealth()
		Return curHealth
	End Method	
	
	Method GetPlayerDist:Int()
		Return playerDist
	End Method
	
End

Function CheckCollision:Bool(e:Enemy,list:List<Enemy>)
	For Local l:= Eachin list
		If e <> l
			If Collide(e.GetX,e.GetY,e.GetWidth(),e.GetHeight(),l.GetX,l.GetY,l.GetWidth(),l.GetHeight()) = True Then
				Return True
			End If
		End If
	Next 
	Return False
End Function

