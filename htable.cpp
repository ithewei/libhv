#include "htable.h"
#include "hdef.h"

HTable::HTable(){
    row = col = 0;
}

void HTable::init(int row, int col){
    this->row = row;
    this->col = col;
    m_mapCells.clear();
    for (int r = 1; r <= row; ++r){
        for (int c = 1; c <= col; ++c){
            int id = (r-1) * col + c;
            m_mapCells[id] = HTableCell(r,r,c,c);
        }
    }
}

bool HTable::getTableCell(int id, HTableCell& rst){
    if (m_mapCells.find(id) != m_mapCells.end()){
        rst = m_mapCells[id];
        return true;
    }
    return false;
}

HTableCell HTable::merge(int lt, int rb){
    HTableCell cell_lt,cell_rb;
    if (getTableCell(lt, cell_lt) && getTableCell(rb, cell_rb)){
        int r1 = MIN(cell_lt.r1, cell_rb.r1);
        int r2 = MAX(cell_lt.r2, cell_rb.r2);
        int c1 = MIN(cell_lt.c1, cell_rb.c1);
        int c2 = MAX(cell_lt.c2, cell_rb.c2);

        HTableCell cell(r1, r2, c1, c2);
        std::map<int, HTableCell>::iterator iter = m_mapCells.begin();
        while (iter != m_mapCells.end()){
            if (cell.contain(iter->second)){
                iter = m_mapCells.erase(iter);
            }else
                ++iter;
        }
        m_mapCells[lt] = cell;

        return cell;
    }

    return HTableCell(0,0,0,0);
}
