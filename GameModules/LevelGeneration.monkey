Import mojo
Import Enemies
Import Ragnarok
Import Items


Class Room
	Field Type:Int
	Field Layout:Int
	Field Neighbours:Int
	Field x:Int
	Field y:Int
	Field nDoor:String = "0"
	Field eDoor:String = "0"
	Field wDoor:String = "0"
	Field sDoor:String = "0"
	Field DoorArray:String[]
	Field RoomTiles:Image = LoadImage("levels/ProtoTileSet.png")
	Field WallLayout:Int[][]
	Field FloorLayout:Int[][]
	Field CollisionArray:Int[][]
	Field RoomSize32:Int = 15
	Field visible:Bool = False
	Field current:Bool = False
	Field complete:Bool = False
	Field enemyList:List<Enemy>
	Field enemyCount:Int
	Field droppedItems:List<Interactables>
	Field droppedWeapons:List<WeaponInterface>
	
	Method New(Type:Int, X:Int, Y:Int)
		Self.x = X
		Self.y = Y
		Self.Type = Type
		droppedItems = New List<Interactables>
		droppedWeapons = New List<WeaponInterface>
		enemyList = New List<Enemy>
		WallLayout =[[0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4], 
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 8, 8, 12, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 4],
						[5, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 6]]
		FloorLayout =[[0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
						[0, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 0],
						[0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0],
						[0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0],
						[0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0],
						[0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0],
						[0, 1, 1, 1, 1, 1, 2, 2, 2, 1, 1, 1, 1, 1, 0],
						[0, 1, 1, 1, 1, 1, 2, 2, 2, 1, 1, 1, 1, 1, 0],
						[0, 1, 1, 1, 1, 1, 2, 2, 2, 1, 1, 1, 1, 1, 0],
						[0, 1, 1, 1, 1, 1, 2, 2, 2, 1, 1, 1, 1, 1, 0],
						[0, 1, 1, 11, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0],
						[0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0],
						[0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0],
						[0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0],
						[0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]]
		CollisionArray =[[1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1],
						[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
						[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
						[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
						[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
						[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
						[1, 0, 0, 0, 0, 0, 2, 2, 2, 0, 0, 0, 0, 0, 1],
						[1, 0, 0, 0, 0, 0, 2, 2, 2, 0, 0, 0, 0, 0, 1],
						[1, 0, 0, 0, 0, 0, 2, 2, 2, 0, 0, 0, 0, 0, 1],
						[1, 0, 0, 0, 0, 0, 2, 2, 2, 0, 0, 0, 0, 0, 1],
						[1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
						[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
						[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
						[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
						[1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1]]
		

	End
	
	Method DrawFloor()
		For Local x:Int = 0 To RoomSize32 - 1
			For Local y:Int = 0 To RoomSize32 - 1
					If FloorLayout[y][x] = 1 Then
						Local floor:Image = RoomTiles.GrabImage(2 * 32, 9 * 32, 32, 32, 1)
						DrawImage(floor, x * 32, y * 32)
					End
					If FloorLayout[y][x] = 2 Then
						Local spike:Image = RoomTiles.GrabImage(2 * 32, 8 * 32, 32, 32, 1)
						DrawImage(spike, x * 32, y * 32)
					End
					If FloorLayout[y][x] = 3 Then
						Local topBrick:Image = RoomTiles.GrabImage(1 * 32, 11 * 32, 32, 32, 1)
						DrawImage(topBrick, x * 32, y * 32)
					End
					If FloorLayout[y][x] = 11 Then
						Local singleBox1:Image = RoomTiles.GrabImage(1 * 32, 7 * 32, 32, 32, 1)
						DrawImage(singleBox1, x * 32, y * 32)
					End If 
			Next
		Next
	End Method
	
	Method DrawWalls()
		For Local x:Int = 0 To RoomSize32 - 1
			For Local y:Int = 0 To RoomSize32 - 1
				Select WallLayout[y][x]
					Case 0
						Local topLeftWall:Image = RoomTiles.GrabImage(0 * 32, 10 * 32, 32, 32, 1)
						DrawImage(topLeftWall, x * 32, y * 32)
					Case 1
						Local wall:Image = RoomTiles.GrabImage(1 * 32, 10 * 32, 32, 32, 1)
						DrawImage(wall, x * 32, y * 32)
					Case 2
						Local topRightWall:Image = RoomTiles.GrabImage(2 * 32, 10 * 32, 32, 32, 1)
						DrawImage(topRightWall, x * 32, y * 32)
					Case 3
						Local leftWall:Image = RoomTiles.GrabImage(0 * 32, 11 * 32, 32, 32, 1)
						DrawImage(leftWall, x * 32, y * 32)
					Case 4
						Local rightWall:Image = RoomTiles.GrabImage(2 * 32, 11 * 32, 32, 32, 1)
						DrawImage(rightWall, x * 32, y * 32)
					Case 5
						Local botLeftWall:Image = RoomTiles.GrabImage(0 * 32, 13 * 32, 32, 32, 1)
						DrawImage(botLeftWall, x * 32, y * 32)
					Case 6
						Local botRightWall:Image = RoomTiles.GrabImage(2 * 32, 13 * 32, 32, 32, 1)
						DrawImage(botRightWall, x * 32, y * 32)
					Case 7
						Local botWall:Image = RoomTiles.GrabImage(1 * 32, 12 * 32, 32, 32, 1)
						DrawImage(botWall, x * 32, y * 32)
					Case 10
						Local botBrick:Image = RoomTiles.GrabImage(1 * 32, 13 * 32, 32, 32, 1)
						DrawImage(botBrick, x * 32, y * 32)
					Case 12
						Local singleBox2:Image = RoomTiles.GrabImage(1 * 32, 6 * 32, 32, 32, 1)
						DrawImage(singleBox2, x * 32, y * 32)
				End Select
			Next
		Next
	End Method
	
	Method DrawEnemies()
		Local debugTextCount:Int = 0
		For Local i:= EachIn enemyList
			i.Draw()
			'DrawText("Enemy " + debugTextCount + ": " + i.GetPlayerDist(),500, 200 + (debugTextCount*20))
			'debugTextCount += 1
		Next 
	End Method
	
	Method DrawItems()
		For Local i:= EachIn droppedItems
			i.Draw()
		Next
		For Local i:= EachIn droppedWeapons
			i.DrawItem()
		Next
	End Method
	
	Method UpdateRoom(player:Player)
		For Local i:= Eachin enemyList
			i.CheckActive(player.GetCentreX(),player.GetCentreY())
			i.CalcMove(player.GetCentreX(),player.GetCentreY())
			i.MoveX()
			If CheckCollision(i,enemyList) = True Then
				i.CorrectX()
			End If
			If Collide(player.GetX(),player.GetY(),player.GetWidth(),player.GetHeight(),i.GetX(),i.GetY(),i.GetWidth(),i.GetHeight()) = True Then
				i.CorrectX()
			End If 
			i.MoveY()
			If CheckCollision(i,enemyList) = True Then
				i.CorrectY()
			End If
			If Collide(player.GetX(),player.GetY(),player.GetWidth(),player.GetHeight(),i.GetX(),i.GetY(),i.GetWidth(),i.GetHeight())= True Then
				i.CorrectY()
			End If
			If i.GetType <> 2 Then
				i.Attack(player)
			Else
				If i.Summon(player) = True And enemyCount <10  Then
					enemyList.AddLast(New Enemy(4, i.GetX() -30, i.GetY(), True))
					enemyList.AddLast(New Enemy(4, i.GetX() +30, i.GetY(), True))
					enemyList.AddLast(New Enemy(4, i.GetX(), i.GetY() +30, True))
					enemyCount += 3
				Endif
			EndIf

			If i.CheckHealth() = 0 Then	
				If player.GetCurHealth < 3 and i.GetType <> 4 Then
					Local chance:Float = Rnd()
					If chance > 0.6 Then
						Local hp:HealthPotion = New HealthPotion(i.GetX, i.GetY)
						droppedItems.AddLast(hp)
					End If
				EndIf
				enemyList.Remove(i)
				enemyCount -= 1
			End If 
		Next
		
		If enemyCount = 0 Then
			complete = True
		End If 
		
		If complete = True Then
			If wDoor = "1" Then
				CollisionArray[7][0] = 0
				WallLayout[7][0] = 8
				FloorLayout[7][0] = 1
			Endif
			If eDoor = "1" Then
				CollisionArray[7][14] = 0
				WallLayout[7][14] = 8
				FloorLayout[7][14] = 1
			Endif
			If nDoor = "1" Then
				CollisionArray[0][7] = 0
				CollisionArray[1][7] = 0
				WallLayout[0][7] = 8
				WallLayout[1][7] = 8
				FloorLayout[0][7] = 1
				FloorLayout[1][7] = 1
			Endif
			If sDoor = "1" Then
				CollisionArray[14][7] = 0
				CollisionArray[13][7] = 0
				WallLayout[13][7] = 8
				WallLayout[14][7] = 8
				FloorLayout[14][7] = 1
				FloorLayout[13][7] = 1
			Endif
		End If 
	End Method
	
	Method Interact(player:Player)
		Local closestItem:Interactables
		Local closestDistItem:Int = 1000
		Local closestWeapon:WeaponInterface
		Local closestDistWeapon:Int = 1000
		
		For Local i:= Eachin droppedItems
			If findDist(player.GetCentreX(), player.GetCentreY(), i.GetX(), i.GetY()) < closestDistItem Then
				closestItem = i
				closestDistItem = findDist(player.GetX(), player.GetY(), i.GetX(), i.GetY())
			End If 
		Next
		
		For Local i:= EachIn droppedWeapons
			If findDist(player.GetCentreX(), player.GetCentreY(), i.GetX(), i.GetY()) < closestDistWeapon Then
				closestWeapon = i
				closestDistWeapon = findDist(player.GetCentreX(), player.GetCentreY(), i.GetX(), i.GetY())
			End If
		Next
		
		If closestDistItem < 30 And closestDistItem < closestDistWeapon Then
			droppedItems.RemoveFirst(closestItem)
			closestItem.PickUp()
			player.AddItem(closestItem)
		End If
		
		If closestDistWeapon < 30 And closestDistWeapon < closestDistItem Then
			droppedWeapons.RemoveFirst(closestWeapon)
			closestWeapon.PickUp()
			player.AddWeapon(closestWeapon)
		End If
		
	End Method

	Method GetDoors:String()
		DoorArray =[nDoor, eDoor, sDoor, wDoor]
		Return ("".Join(DoorArray))
	End
	
	Method SetLayout(_layout:Int)
		Select _layout
			Case 0
				enemyList.Clear()
				Local largeSword:Sword = New Sword(10, "items/largeSword.png", 30, 12, 1)
				droppedWeapons.AddLast(largeSword)
				FloorLayout = [[0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
						[0, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 0],
						[0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0],
						[0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0],
						[0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0],
						[0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0],
						[0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0],
						[0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0],
						[0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0],
						[0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0],
						[0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0],
						[0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0],
						[0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0],
						[0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0],
						[0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]]
				CollisionArray =[[1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1],
						[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
						[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
						[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
						[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
						[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
						[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
						[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
						[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
						[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
						[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
						[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
						[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
						[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
						[1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1]]
				WallLayout =[[0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4], 
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 4],
						[5, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 6]]
			Case 1
			WallLayout =[[0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4], 
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 8, 8, 12, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 4],
						[5, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 6]]
		FloorLayout =[[0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
						[0, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 0],
						[0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0],
						[0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0],
						[0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0],
						[0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0],
						[0, 1, 1, 1, 1, 1, 2, 2, 2, 1, 1, 1, 1, 1, 0],
						[0, 1, 1, 1, 1, 1, 2, 2, 2, 1, 1, 1, 1, 1, 0],
						[0, 1, 1, 1, 1, 1, 2, 2, 2, 1, 1, 1, 1, 1, 0],
						[0, 1, 1, 1, 1, 1, 2, 2, 2, 1, 1, 1, 1, 1, 0],
						[0, 1, 1, 11, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0],
						[0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0],
						[0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0],
						[0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0],
						[0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]]
		CollisionArray =[[1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1],
						[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
						[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
						[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
						[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
						[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
						[1, 0, 0, 0, 0, 0, 2, 2, 2, 0, 0, 0, 0, 0, 1],
						[1, 0, 0, 0, 0, 0, 2, 2, 2, 0, 0, 0, 0, 0, 1],
						[1, 0, 0, 0, 0, 0, 2, 2, 2, 0, 0, 0, 0, 0, 1],
						[1, 0, 0, 0, 0, 0, 2, 2, 2, 0, 0, 0, 0, 0, 1],
						[1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
						[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
						[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
						[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
						[1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1]]
				enemyList.Clear()
				enemyCount = 3
				enemyList.AddLast(New Enemy(1, 250, 360, False))
				enemyList.AddLast(New Enemy(2, 100, 200, False))
				enemyList.AddLast(New Enemy(1, 350, 350, False))
			Case 3
								FloorLayout = [[0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
						[0, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 0],
						[0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0],
						[0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0],
						[0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0],
						[0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0],
						[0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0],
						[0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0],
						[0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0],
						[0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0],
						[0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0],
						[0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0],
						[0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0],
						[0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0],
						[0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]]
				CollisionArray =[[1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1],
						[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
						[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
						[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
						[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
						[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
						[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
						[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
						[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
						[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
						[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
						[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
						[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
						[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
						[1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1]]
				WallLayout =[[0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4], 
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 4],
						[5, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 6]]
				enemyList.Clear()
				enemyCount = 1
				enemyList.AddLast(New Enemy(10, 100, 250, False))
		End
	End
	Method SetnDoor(_nDoor:String)
		Self.nDoor = _nDoor
	End
	
	Method SetsDoor(_sDoor:String)
		Self.sDoor = _sDoor
	End
	
	Method SeteDoor(_eDoor:String)
		Self.eDoor = _eDoor
	End
	
	Method SetwDoor(_wDoor:String)
		Self.wDoor = _wDoor
	End
	
	Method BuildDoors()
		If wDoor = "1" Then
			CollisionArray[7][0] = 0
			WallLayout[7][0] = 8
			FloorLayout[7][0] = 8
		Endif
		If eDoor = "1" Then
			CollisionArray[7][14] = 0
			WallLayout[7][14] = 8
			FloorLayout[7][14] = 8
		Endif
		If nDoor = "1" Then
			CollisionArray[0][7] = 0
			CollisionArray[1][7] = 0
			WallLayout[0][7] = 8
			WallLayout[1][7] = 8
			FloorLayout[0][7] = 8
			FloorLayout[1][7] = 8
		Endif
		If sDoor = "1" Then
			CollisionArray[14][7] = 0
			CollisionArray[13][7] = 0
			WallLayout[13][7] = 8
			WallLayout[14][7] = 8
			FloorLayout[14][7] = 8
			FloorLayout[13][7] = 8
		Endif
	End
	
	Method UpdateNeighbours(Amount:Int)
		Self.Neighbours += Amount
	End
	
	Method UpdateType(Type:Int)
		Self.Type = Type
	End

	Method UpdateCurrent(_current:Bool)
		current = _current
	End Method
	
	Method UpdateVisible(_visible:Bool)
		visible = _visible
	End Method
	
	Method GetNeighbours()
		Return Neighbours
	End
	
	Method GetType()
		Return Type
	End
	
	Method GetVisible:bool()
		Return visible
	End
	
	Method GetCurrent:Bool()
		Return current
	End Method
	
	Method GetEnemyList:List<Enemy>()
		Return enemyList
	End Method
	Method GetX()
		Return Self.x
	End
	
	Method GetY()
		Return Self.y
	End
	Method Reset()
		Self.Type = 0
		Self.Neighbours = 0
		Self.nDoor = "0"
		Self.eDoor = "0"
		Self.sDoor = "0"
		Self.wDoor = "0"
		visible = False
		WallLayout =[[0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2],
						[3, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 4], 
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 4],
						[5, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 6]]
		FloorLayout =[[0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2],
						[3, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 4],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4],
						[5, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 6]]
		CollisionArray =[[1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1],
						[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
						[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
						[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
						[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
						[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
						[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
						[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
						[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
						[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
						[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
						[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
						[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
						[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
						[1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1]]
	End
	
	Method TileCollision:Bool(x1:Float, y1:Float)
		Local tileLeft:Int = (x1 / 32)
		Local tileRight:Int = ( (x1 + 19) / 32)
		Local tileTop:Int = (y1 / 32)
		Local tileBot:Int = ( (y1 + 29) / 32)
		
		If tileLeft < 0 Then tileLeft = 0
		If tileRight > 15 Then tileRight = 15
		If tileTop < 0 Then tileTop = 0
		If tileBot > 15 Then tileBot = 15
		
		For Local i:Int = tileLeft To tileRight
			For Local j:Int = tileTop To tileBot
				If CollisionArray[j][i] = 1 Then Return True
			Next
		Next
		Return False
	End Method
	
	Method DamageCollision:Bool(x1:Float, y1:Float)
		Local tileLeft:Int = (x1 / 32)
		Local tileRight:Int = ( (x1 + 19) / 32)
		Local tileTop:Int = (y1 / 32)
		Local tileBot:Int = ( (y1 + 29) / 32)
		
		If tileLeft < 0 Then tileLeft = 0
		If tileRight > 15 Then tileRight = 15
		If tileTop < 0 Then tileTop = 0
		If tileBot > 15 Then tileBot = 15
		
		For Local i:Int = tileLeft To tileRight
			For Local j:Int = tileTop To tileBot
				If CollisionArray[j][i] = 2 Then Return True
			Next
		Next
		Return False
	End Method
	
End



Class RoomMap
	Field Map:Room[6][]
	Field MapHeight = 6
	Field MapWidth = 6
	Field MapPSize = 25
	Field MapXOffset:Int = 485
	Field roomNum:Int = 8
	Field currentRoom:Room
	Field mapBorder:Image = LoadImage("gui/mapBorder.png")
	
	Method Build()
		Local RoomCount:Int = 0
		'Setting up room array
		For Local i = 0 To MapWidth - 1
			Map[i] = New Room[MapHeight]
		Next
		
		'Creating room objects
		For Local i = 0 To MapWidth - 1
			For Local j = 0 To MapHeight - 1
				Map[i][j] = New Room(0,i,j)
			Next
		Next
		
		
		'Finding the starting room
		Local xr:Int = (Rnd() * MapWidth)
		Local yr:Int = (Rnd() * MapHeight)
		Map[xr][yr].UpdateType(1)
		currentRoom = Map[xr][yr]
		currentRoom.UpdateVisible(True)
		currentRoom.UpdateCurrent(True)
		currentRoom.SetLayout(0)
		RoomCount += 1
		'Updating Neighbour Count for surrounding rooms
		If xr > 0 And xr < MapWidth - 1
			Map[xr+1][yr].UpdateNeighbours(1)
			Map[xr-1][yr].UpdateNeighbours(1)
		Elseif xr = 0
			Map[xr+1][yr].UpdateNeighbours(1)
		Elseif xr = MapWidth - 1
			Map[xr-1][yr].UpdateNeighbours(1)
		End
		
		If yr > 0 And yr < MapHeight - 1
			Map[xr][yr+1].UpdateNeighbours(1)
			Map[xr][yr-1].UpdateNeighbours(1)
		Elseif yr = 0
			Map[xr][yr+1].UpdateNeighbours(1)
		Elseif yr = MapHeight - 1
			Map[xr][yr-1].UpdateNeighbours(1)
		End
		
	'Filling out rest of rooms	
	While RoomCount < roomNum
		Local Valid:List<Room> = New List<Room>
		'Checks for Valid Next Rooms
		For Local i = 0 To MapWidth - 1
			For Local j = 0 To MapHeight - 1
				If Map[i][j].GetNeighbours = 1 And Map[i][j].GetType = 0
					Valid.AddLast Map[i][j]
				End
			Next
		Next
		
		'Converts list to array
		Local validRoomArray:Room[] = Valid.ToArray
		
		'Choose random room from list, update its type and increase room count
		Local ir:Int = (Rnd() * validRoomArray.Length - 1)
		validRoomArray[ir].UpdateType(2)
		validRoomArray[ir].SetLayout(1)
		RoomCount += 1
		
		'Find the x and y values of the chosen room
		Local x:Int = validRoomArray[ir].GetX()
		Local y:Int = validRoomArray[ir].GetY()
		
			'Update surrounding rooms neighbour value
			If x > 0 And x < MapWidth - 1
				Map[x + 1][y].UpdateNeighbours(1)
				Map[x - 1][y].UpdateNeighbours(1)
			ElseIf x = 0
				Map[x + 1][y].UpdateNeighbours(1)
			Elseif x = MapWidth - 1
				Map[x - 1][y].UpdateNeighbours(1)
			End
		
			If y > 0 And y < MapHeight - 1
				Map[x][y + 1].UpdateNeighbours(1)
				Map[x][y - 1].UpdateNeighbours(1)
			ElseIf y = 0
				Map[x][y + 1].UpdateNeighbours(1)
			Elseif y = MapHeight - 1
				Map[x][y - 1].UpdateNeighbours(1)
			End
	
	End While
	
	'Create boss/escape room
	Local EndRooms:List<Room>
	EndRooms = New List<Room>
	
	For Local i = 0 To MapWidth - 1
		For Local j = 0 To MapHeight - 1
			If Map[i][j].GetNeighbours = 1 And Map[i][j].GetType = 2
				EndRooms.AddLast Map[i][j]
			End
		Next
	Next
	
	Local EndRoomsArray:Room[] = EndRooms.ToArray
	Local Boss:Int = (Rnd() * EndRoomsArray.Length - 1)
	
	EndRoomsArray[Boss].UpdateType(3)
	EndRoomsArray[Boss].SetLayout(3)
	
	
	'Find where doors need to be placed in each room
	For Local x:Int = 0 To MapWidth - 1
		For Local y:Int = 0 To MapHeight - 1
			If Map[x][y].GetType <> 0
				If y > 0 Then
					If Map[x][y - 1].GetType <> 0 And y > 0 Then
						Map[x][y].SetnDoor("1")
					EndIf
				EndIf
				If y < MapHeight - 1 Then
					If Map[x][y + 1].GetType <> 0 And y < MapHeight - 1 Then
						Map[x][y].SetsDoor("1")
					EndIf
				EndIf
				If x > 0 Then
					If Map[x - 1][y].GetType <> 0 and x > 0 Then
						Map[x][y].SetwDoor("1")
					EndIf
				EndIf
				If x < MapWidth - 1
					If Map[x + 1][y].GetType <> 0 And x < MapWidth - 1 Then
						Map[x][y].SeteDoor("1")
					EndIf
				EndIf
			EndIf
		Next
	Next
	
	'For Local x:Int = 0 To MapWidth - 1
'		For Local y:Int = 0 To MapWidth -1
		'	Map[x][y].BuildDoors()
	'	Next
'	Next
	
	End
	
	Method DrawRoomFloor()
		currentRoom.DrawFloor()
		currentRoom.DrawEnemies()
		currentRoom.DrawItems()
	End
	
	Method DrawRoomWalls()
		currentRoom.DrawWalls()
	End
	
	Method InteractCurrentRoom(player:Player)
		currentRoom.Interact(player)
	End
	
	Method UpdateCurrentRoom(player:Player)
		currentRoom.UpdateRoom(player)
	End
	
	Method GetCurrentRoomEnemies:List<Enemy>()
		Return currentRoom.GetEnemyList()
	End 
	
	Method TileCollision:Bool(x:Float,y:Float)
		Return currentRoom.TileCollision(x,y)
	End
	
	Method DamageCollision:Bool(x:Float,y:Float)
		Return currentRoom.DamageCollision(x,y)
	End
	
	Method MoveRoom(direction:String)
		Select direction
			Case "NORTH"
				currentRoom.UpdateCurrent(False)
				currentRoom = Map[currentRoom.GetX()][currentRoom.GetY()-1]
				currentRoom.UpdateVisible(True)
				currentRoom.UpdateCurrent(True)
			Case "SOUTH"
				currentRoom.UpdateCurrent(False)
				currentRoom = Map[currentRoom.GetX()][currentRoom.GetY()+1]
				currentRoom.UpdateVisible(True)
				currentRoom.UpdateCurrent(True)
			Case "EAST"
				currentRoom.UpdateCurrent(False)
				currentRoom = Map[currentRoom.GetX() - 1][currentRoom.GetY()]
				currentRoom.UpdateVisible(True)
				currentRoom.UpdateCurrent(True)
			Case "WEST"
				currentRoom.UpdateCurrent(False)
				currentRoom = Map[currentRoom.GetX()+1][currentRoom.GetY()]
				currentRoom.UpdateVisible(True)
				currentRoom.UpdateCurrent(True)
		End

	End
	
	Method DrawMap()
		'Draws current map to the screen
		DrawImage(mapBorder,480,0)
		For Local x = 0 To MapWidth - 1
			For Local y = 0 To MapHeight - 1
				If Map[x][y].GetVisible() = True
				Select Map[x][y].GetType
					Case 0 'blank room
						SetColor(0, 0, 0)
						DrawRect((x * MapPSize) + MapXOffset, y * MapPSize, MapPSize, MapPSize)
					Case 1 'start room
						If Map[x][y].GetCurrent = True Then
							SetColor(255, 255, 255)
							DrawRect((x * MapPSize) + MapXOffset, y * MapPSize, MapPSize, MapPSize)
						End If
						DrawRoom("levels/StartRoom/" + Map[x][y].GetDoors + ".png", x, y, MapPSize, MapXOffset)
					Case 2 'filled room
						If Map[x][y].GetCurrent = True Then
							SetColor(255, 255, 255)
							DrawRect((x * MapPSize) + MapXOffset, y * MapPSize, MapPSize, MapPSize)
						End If
						DrawRoom("levels/Room/" + Map[x][y].GetDoors + ".png", x, y, MapPSize, MapXOffset)
					Case 3 'Boss Room
						If Map[x][y].GetCurrent = True Then
							SetColor(255, 255, 255)
							DrawRect((x * MapPSize) + MapXOffset, y * MapPSize, MapPSize, MapPSize)
						End If
						DrawRoom("levels/BossRoom/" + Map[x][y].GetDoors + ".png", x,y,MapPSize,MapXOffset)
					End
				
				End
			Next
		Next
		
	End
	
	Method Reset()
	'used to reset map array to create new map
		For Local x = 0 To MapWidth - 1
			For Local y = 0 To MapHeight - 1
				Map[x][y].Reset()
			Next
		Next
	End
End

Function DrawRoom(path:String, x:Int, y:Int, mapPSize:Int, mapXOffset:Int)
	Local Room:Image
	Local ImageSize:Int = 25
	Room = LoadImage(path, ImageSize, ImageSize, 1)
	DrawImage(Room, (x * mapPSize) + mapXOffset, y * mapPSize)
End

'#####################################################################################################
'########################################END OF LEVEL GENERATION######################################
'#####################################################################################################