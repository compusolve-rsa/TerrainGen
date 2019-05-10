// Fill out your copyright notice in the Description page of Project Settings.

#include "ProceduralTerrain.h"

// Sets default values
AProceduralTerrain::AProceduralTerrain() {
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	seed = rand();

	noiseGen.SetSeed(seed);
	noiseGen.SetNoiseType(FastNoise::SimplexFractal);
	noiseGen.SetFractalOctaves(6);
}

// Called when the game starts or when spawned
void AProceduralTerrain::BeginPlay() {
	Super::BeginPlay();

	frequency = 256 / Scale;
	heightScale = Scale * HeightToWidthRatio;

	noiseGen.SetFrequency(0.0000625*frequency/16);
	infoWorker = ChunkInfoWorker(GetParams());
	infoWorker.infoMapPtr = &infoMap;
	infoWorker.GenerateRadius = RenderRadius * 1.50;

	infoWorker.Params.TerrainCurve = TerrainCurve;
	infoWorker.playerPos = GetWorld()->GetFirstPlayerController()->GetPawn()->GetActorLocation();
	infoWorker.ChunkDeletion = &ChunkDeletion;

	infoWorkerThread = FRunnableThread::Create(&infoWorker, TEXT("ChunkInfoWorker"), 0, TPri_BelowNormal);
}

ChunkGenParams AProceduralTerrain::GetParams() {
	return ChunkGenParams(ChunkResolution, ChunkSize, &noiseGen, TerrainCurve, heightScale);
}

void AProceduralTerrain::EndPlay(const EEndPlayReason::Type EndPlayReason) {
	infoWorkerThread->Kill(true);
}

// Called every frame
void AProceduralTerrain::Tick(float DeltaTime) {
	Super::Tick(DeltaTime);

	FVector playerPos = GetWorld()->GetFirstPlayerController()->GetPawn()->GetActorLocation();
	infoWorker.playerPos = playerPos;
	cullAndSpawnChunks(FVector2D(playerPos));
}

void AProceduralTerrain::spawnChunk(int x, int y) {
	// first, check if this chunk has already been spawned
	TPair<int, int> chunkPos(x, y);
	{
		FScopeLock LockWhileSpawning(&ChunkDeletion);
		if (!chunkMap.Contains(chunkPos) && infoMap.Contains(chunkPos)) {
			// get cached chunk info
			ChunkInfo* chunkInfo = infoMap.Find(chunkPos);

			auto procMesh = NewObject<UProceduralMeshComponent>(this);
			auto world = GetWorld();
			procMesh->SetWorldLocation(FVector(x*ChunkSize, y*ChunkSize, 0));
			procMesh->CreateMeshSection_LinearColor(0, chunkInfo->GetVertices(), chunkInfo->GetTriangles(), chunkInfo->GetNormals(), chunkInfo->GetUVMap(), chunkInfo->GetColors(), chunkInfo->GetTangents(), false);
			procMesh->SetMaterial(0, TerrainMaterial);
			procMesh->RegisterComponentWithWorld(world);
			chunkMap.Add(TPair<int, int>(x, y), procMesh);
		}
	}
}

void AProceduralTerrain::cullAndSpawnChunks(FVector2D playerLocation) {
	float renderRadiusSquare = powf(RenderRadius, 2);

	// spawn chunks
	int xStart = (playerLocation.X - RenderRadius) / ChunkSize;
	int xEnd = (playerLocation.X + RenderRadius) / ChunkSize;
	int yStart = (playerLocation.Y - RenderRadius) / ChunkSize;
	int yEnd = (playerLocation.Y + RenderRadius) / ChunkSize;
	// iterate through all chunks in (playerLocation.X - RenderRadius, playerLocation.X + RenderRadius)
	for (int x = xStart; x < xEnd; x++) {
		// iterate through all chunks in (playerLocation.Y - RenderRadius, playerLocation.Y + RenderRadius)
		for (int y = yStart; y < yEnd; y++) {
			// check that the chunk is within radius
			float distanceFromPlayerSquare = powf((x*ChunkSize - playerLocation.X), 2) + powf((y*ChunkSize - playerLocation.Y), 2);
			if (distanceFromPlayerSquare < renderRadiusSquare) {
				spawnChunk(x, y);
			}
		}
	}

	// cull chunks
	TArray<TPair<int, int>> chunksToRemove;
	for (auto& Elem : chunkMap) {
		// pythagoras
		float distanceFromPlayerSquare = powf((Elem.Key.Key*ChunkSize - playerLocation.X), 2) + powf((Elem.Key.Value*ChunkSize - playerLocation.Y), 2);
		// destroy chunk if its further than the render radius
		if ((distanceFromPlayerSquare + 10000.0f) > renderRadiusSquare) {
			chunksToRemove.Add(TPair<int, int>(Elem.Key.Key, Elem.Key.Value));
		}
	}

	{
		FScopeLock LockWhileRemoving(&ChunkDeletion);
		for (auto chunk : chunksToRemove) {
			auto chunkPtr = *(chunkMap.Find(chunk));
			chunkMap.Remove(chunk);
			chunkPtr->UnregisterComponent();
			chunkPtr->DestroyComponent();
		}
	}

	chunkMap.Compact();
	chunkMap.Shrink();
}

void AProceduralTerrain::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) {
	// destroy all chunks so that they can be regenerated with new properties
	frequency = 256 / Scale;
	heightScale = Scale * HeightToWidthRatio;
	UE_LOG(LogTemp, Log, TEXT("Property changed"));
	infoWorker.Params = GetParams();
	infoWorker.GenerateRadius = RenderRadius * 1.50;
	{
		FScopeLock LockWhileRemoving(&ChunkDeletion);
		for (auto& Elem : chunkMap) {
			Elem.Value->UnregisterComponent();
			Elem.Value->DestroyComponent();
		}
		chunkMap.Empty();
		infoMap.Empty();
	}
}
