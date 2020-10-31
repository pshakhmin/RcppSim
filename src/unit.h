#pragma once

#include "defines.h"

#include "chunk.h"
#include "grid.h"

template<size_t dim>
class Unit {
    Chunk<dim>& chunk;
    double& chunkDeathRate;
    size_t& chunkPopulation;
    const Position<dim> chunkPos;
    const size_t i;

public:
    Unit(Grid<dim>& grid, Position<dim> chunkPosition, size_t i)
        : chunk(grid.GetChunk(chunkPosition))
        , chunkDeathRate(grid.GetChunkDeathRate(chunkPosition, chunk.species[i]))
        , chunkPopulation(grid.GetChunkPopulation(chunkPosition, chunk.species[i]))
        , chunkPos(chunkPosition)
        , i(i)
    {}
    
public:
    const Coord<dim>& Coord() const;
    double& DeathRate();
    size_t Species() const;
    double& ChunkDeathRate();
    size_t& ChunkPopulation();
    const Position<dim>& ChunkPosition() const;
    
    bool operator==(const Unit<dim>& other);
};
